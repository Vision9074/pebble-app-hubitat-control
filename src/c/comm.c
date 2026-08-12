#include "comm.h"

#include <string.h>

#include "app.h"
#include "devices.h"

// Watch -> phone operations, sent in MESSAGE_KEY_Req.
#define OP_HELLO 0
#define OP_LIST 1
#define OP_REFRESH 2
#define OP_STATES 3
#define OP_COMMAND 4
#define OP_MODE 5
#define OP_DEVICE 6

// Phone -> watch messages, sent in MESSAGE_KEY_Msg.
#define MSG_READY 0
#define MSG_LIST_CHUNK 1
#define MSG_STATE_CHUNK 2
#define MSG_STATUS 3
#define MSG_LIST_BEGIN 4
#define MSG_LIST_END 5

// Status codes carried in MESSAGE_KEY_Num alongside MSG_STATUS.
#define STATUS_IDLE 0
#define STATUS_LOADING 1
#define STATUS_ERROR 2

#define STATUS_TEXT_LEN 48
#define QUEUE_LEN 8
#define SEND_RETRIES 3
#define RETRY_DELAY_MS 350

// The phone may not have started its JS yet when the app opens, so the first
// hello is repeated until something comes back.
#define HELLO_RETRY_MS 2500
#define HELLO_ATTEMPTS 6

// How long a link may be down before anything still queued is treated as stale
// intent and thrown away rather than delivered late. A real Bluetooth blip is a
// second or two; beyond this the user has walked off, and a command they pressed
// before losing the link - an unlock, say - must not fire behind them. Kept
// short deliberately, because the disconnect event can itself arrive late, which
// makes the measured gap shorter than the real one.
#define STALE_QUEUE_SECONDS 10

#define PERSIST_KEY_MODE 1

typedef struct {
  int32_t op;
  int32_t id;
  int32_t arg;
  char cmd[10];
} Request;

static Request s_queue[QUEUE_LEN];
static int s_queue_head;
static int s_queue_len;

static Request s_current;
static bool s_have_current;
static bool s_in_flight;
static int s_attempts;
static AppTimer *s_retry_timer;

static AppTimer *s_hello_timer;
static int s_hello_attempts;

// When the link went down, so a reconnect can tell a blip from an absence.
static time_t s_disconnected_at;

static LinkState s_link = LinkWaiting;
static char s_status[STATUS_TEXT_LEN] = "Connecting...";
static int s_mode = MODE_LOCAL;
static int s_step = 5;
static uint32_t s_inbox_size = 512;
// Assumed until the phone says otherwise, so the row is never wrongly disabled.
static bool s_cloud_ok = true;
// The opposite default: opening a lock is only ever allowed once opted in.
static bool s_quick_lock = false;

static void pump(void);

static void set_status(LinkState state, const char *text) {
  s_link = state;
  snprintf(s_status, sizeof(s_status), "%s", text);
  app_status_changed();
}

// ---------------------------------------------------------------- outbox ---

static void enqueue(int32_t op, int32_t id, const char *cmd, int32_t arg) {
  if (s_queue_len == QUEUE_LEN) {
    // Dropping the oldest keeps a burst of button presses responsive; the
    // newest intent is the one worth honouring.
    s_queue_head = (s_queue_head + 1) % QUEUE_LEN;
    s_queue_len--;
  }

  Request *r = &s_queue[(s_queue_head + s_queue_len) % QUEUE_LEN];
  r->op = op;
  r->id = id;
  r->arg = arg;
  snprintf(r->cmd, sizeof(r->cmd), "%s", cmd ? cmd : "");
  s_queue_len++;

  pump();
}

static void retry_timer(void *data) {
  s_retry_timer = NULL;
  pump();
}

static bool queue_has_work(void) {
  return s_have_current || s_queue_len > 0;
}

// Drops everything waiting, including the request already pulled off the queue.
static void queue_clear(void) {
  s_queue_head = 0;
  s_queue_len = 0;
  s_have_current = false;
  s_attempts = 0;
}

static void pump(void) {
  if (s_in_flight)
    return;

  // Nothing can leave the watch while the phone is away, and retrying into a
  // dead link is pure radio cost for no chance of success. Requests stay queued
  // and connection_handler resumes them the moment the link is back.
  if (!connection_service_peek_pebble_app_connection())
    return;

  if (!s_have_current) {
    if (s_queue_len == 0)
      return;
    s_current = s_queue[s_queue_head];
    s_queue_head = (s_queue_head + 1) % QUEUE_LEN;
    s_queue_len--;
    s_have_current = true;
    s_attempts = 0;
  }

  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) != APP_MSG_OK) {
    if (!s_retry_timer)
      s_retry_timer = app_timer_register(RETRY_DELAY_MS, retry_timer, NULL);
    return;
  }

  dict_write_int32(out, MESSAGE_KEY_Req, s_current.op);
  dict_write_int32(out, MESSAGE_KEY_ReqId, s_current.id);
  dict_write_int32(out, MESSAGE_KEY_ReqArg, s_current.arg);
  if (s_current.cmd[0])
    dict_write_cstring(out, MESSAGE_KEY_ReqCmd, s_current.cmd);
  dict_write_end(out);

  s_attempts++;
  s_in_flight = true;

  if (app_message_outbox_send() != APP_MSG_OK) {
    s_in_flight = false;
    if (!s_retry_timer)
      s_retry_timer = app_timer_register(RETRY_DELAY_MS, retry_timer, NULL);
  }
}

static void outbox_sent(DictionaryIterator *iter, void *context) {
  s_in_flight = false;
  s_have_current = false;
  pump();
}

static void outbox_failed(DictionaryIterator *iter, AppMessageResult reason,
                          void *context) {
  s_in_flight = false;

  if (s_attempts >= SEND_RETRIES) {
    s_have_current = false;
    if (s_link != LinkError)
      set_status(LinkError, "Phone unreachable");
  }

  // Only book another wakeup if something is actually still waiting to go.
  if (!s_retry_timer && (s_have_current || s_queue_len > 0))
    s_retry_timer = app_timer_register(RETRY_DELAY_MS, retry_timer, NULL);
}

// The link coming back is an event, not something to poll for; this is what
// makes it safe for pump() to simply stop while the phone is away.
static void connection_handler(bool connected) {
  if (!connected) {
    s_disconnected_at = time(NULL);
    set_status(LinkError, "Phone disconnected");
    return;
  }

  const bool stale = s_disconnected_at != 0 &&
                     (time(NULL) - s_disconnected_at) > STALE_QUEUE_SECONDS;
  const bool had_work = queue_has_work();
  s_disconnected_at = 0;

  if (stale) {
    // A button pressed before the link dropped is no longer a safe statement of
    // intent: the user has moved on and the hub may have changed underneath.
    // Throw it away rather than acting on it minutes late.
    queue_clear();
    set_status(LinkLoading,
               had_work ? "Dropped stale actions" : "Reconnected");
  } else {
    set_status(devices_count() ? LinkReady : LinkLoading,
               devices_count() ? "Ready" : "Reconnecting...");
  }

  pump();

  // Come back to the truth rather than to whatever the watch last assumed. A
  // launch that happened out of range never got its list at all.
  if (devices_count() == 0)
    comm_request_list(false);
  else if (stale)
    comm_request_states();
}

// ----------------------------------------------------------------- inbox ---

static void cancel_hello(void) {
  if (s_hello_timer) {
    app_timer_cancel(s_hello_timer);
    s_hello_timer = NULL;
  }
}

static void send_hello(void) {
  // The phone sizes its chunks to the inbox this watch managed to allocate and
  // truncates the list to the number of rows this watch can hold.
  enqueue(OP_HELLO, DEVICE_LIMIT, NULL, (int32_t)s_inbox_size);
}

static void hello_timer(void *data) {
  s_hello_timer = NULL;
  if (s_link != LinkWaiting)
    return;

  // Out of range there is nothing to hand off to; connection_handler picks it
  // up again rather than this burning six retries into the air.
  if (!connection_service_peek_pebble_app_connection()) {
    set_status(LinkError, "Phone disconnected");
    return;
  }

  if (++s_hello_attempts > HELLO_ATTEMPTS) {
    set_status(LinkError, "No phone connection");
    return;
  }

  send_hello();
  s_hello_timer = app_timer_register(HELLO_RETRY_MS, hello_timer, NULL);
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *t_msg = dict_find(iter, MESSAGE_KEY_Msg);
  if (!t_msg)
    return;

  Tuple *t_num = dict_find(iter, MESSAGE_KEY_Num);
  Tuple *t_payload = dict_find(iter, MESSAGE_KEY_Payload);
  const int32_t num = t_num ? t_num->value->int32 : 0;
  const char *payload = t_payload ? t_payload->value->cstring : NULL;

  // The phone owns the mode; the watch keeps a copy only so the menu can render
  // before the first message arrives.
  Tuple *t_mode = dict_find(iter, MESSAGE_KEY_Mode);
  if (t_mode) {
    const int mode = t_mode->value->int32 ? MODE_CLOUD : MODE_LOCAL;
    if (mode != s_mode) {
      s_mode = mode;
      persist_write_int(PERSIST_KEY_MODE, s_mode);
    }
  }

  Tuple *t_step = dict_find(iter, MESSAGE_KEY_Step);
  if (t_step && t_step->value->int32 > 0)
    s_step = t_step->value->int32;

  Tuple *t_cloud = dict_find(iter, MESSAGE_KEY_CloudOk);
  if (t_cloud)
    s_cloud_ok = t_cloud->value->int32 != 0;

  Tuple *t_quick = dict_find(iter, MESSAGE_KEY_QuickLock);
  if (t_quick)
    s_quick_lock = t_quick->value->int32 != 0;

  switch (t_msg->value->int32) {
    case MSG_READY:
      // The phone's JS came up. Ask for the list now rather than waiting out
      // the retry timer.
      cancel_hello();
      send_hello();
      comm_request_list(false);
      set_status(LinkLoading, "Loading devices...");
      break;

    case MSG_LIST_BEGIN:
      cancel_hello();
      if (!devices_reserve(num))
        set_status(LinkError, "Out of memory");
      else
        set_status(LinkLoading, "Loading devices...");
      app_devices_reloaded();
      break;

    case MSG_LIST_CHUNK:
      cancel_hello();
      devices_parse_list_chunk(num, payload);
      app_devices_reloaded();
      break;

    case MSG_LIST_END:
      set_status(LinkReady, "Ready");
      app_devices_reloaded();
      break;

    case MSG_STATE_CHUNK:
      cancel_hello();
      devices_parse_state_chunk(payload);
      app_states_updated();
      break;

    case MSG_STATUS:
      cancel_hello();
      if (num == STATUS_ERROR)
        set_status(LinkError, payload ? payload : "Hub error");
      else if (num == STATUS_LOADING)
        set_status(LinkLoading, payload ? payload : "Working...");
      else
        set_status(LinkReady, payload ? payload : "Ready");
      break;

    default:
      break;
  }
}

static void inbox_dropped(AppMessageResult reason, void *context) {
  // A dropped chunk leaves a gap the next full refresh will fill; saying so is
  // more useful than silently showing a short list.
  set_status(LinkError, "Message dropped");
}

// ------------------------------------------------------------------- api ---

// Asks for the largest inbox the system will hand over, stepping down until one
// is granted. Aplite typically settles well below the others.
static void open_app_message(void) {
  static const uint32_t sizes[] = {2048, 1024, 512, 256, 124};

  for (unsigned i = 0; i < ARRAY_LENGTH(sizes); i++) {
    if (app_message_open(sizes[i], 256) == APP_MSG_OK) {
      s_inbox_size = sizes[i];
      return;
    }
  }

  s_inbox_size = 0;
  set_status(LinkError, "AppMessage failed");
}

void comm_init(void) {
  if (persist_exists(PERSIST_KEY_MODE))
    s_mode = persist_read_int(PERSIST_KEY_MODE) ? MODE_CLOUD : MODE_LOCAL;

  app_message_register_inbox_received(inbox_received);
  app_message_register_inbox_dropped(inbox_dropped);
  app_message_register_outbox_sent(outbox_sent);
  app_message_register_outbox_failed(outbox_failed);
  open_app_message();

  if (s_inbox_size == 0)
    return;

  // Free and event-driven - it never polls, and it lets the retry paths stop
  // dead while the phone is out of range instead of retrying into nothing.
  connection_service_subscribe((ConnectionHandlers){
    .pebble_app_connection_handler = connection_handler
  });

  if (!connection_service_peek_pebble_app_connection()) {
    set_status(LinkError, "Phone disconnected");
    return;
  }

  send_hello();
  comm_request_list(false);

  s_hello_attempts = 0;
  s_hello_timer = app_timer_register(HELLO_RETRY_MS, hello_timer, NULL);
}

void comm_deinit(void) {
  cancel_hello();
  if (s_retry_timer) {
    app_timer_cancel(s_retry_timer);
    s_retry_timer = NULL;
  }
  connection_service_unsubscribe();
  app_message_deregister_callbacks();
}

void comm_request_list(bool force_refresh) {
  if (force_refresh)
    set_status(LinkLoading, "Reading hub...");
  enqueue(force_refresh ? OP_REFRESH : OP_LIST, 0, NULL, 0);
}

void comm_request_states(void) {
  enqueue(OP_STATES, 0, NULL, 0);
}

void comm_request_device(int32_t device_id) {
  enqueue(OP_DEVICE, device_id, NULL, 0);
}

void comm_send_command(int32_t device_id, const char *cmd, int32_t arg) {
  enqueue(OP_COMMAND, device_id, cmd, arg);
}

void comm_set_mode(int mode) {
  s_mode = mode ? MODE_CLOUD : MODE_LOCAL;
  persist_write_int(PERSIST_KEY_MODE, s_mode);
  enqueue(OP_MODE, 0, NULL, s_mode);
  app_status_changed();
}

int comm_mode(void) {
  return s_mode;
}

bool comm_cloud_available(void) {
  return s_cloud_ok;
}

bool comm_quick_lock_allowed(void) {
  return s_quick_lock;
}

LinkState comm_link_state(void) {
  return s_link;
}

const char *comm_status_text(void) {
  return s_status;
}

int comm_step(void) {
  return s_step;
}
