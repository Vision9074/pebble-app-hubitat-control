#include "devices.h"

#include <stdlib.h>
#include <string.h>

#define FIELD_SEP '\x1f'
#define REC_SEP '\x1e'

static Device *s_devices;
static int s_capacity;
static int s_count;
static time_t s_states_at;

bool devices_reserve(int count) {
  devices_free();

  if (count <= 0)
    return true;
  if (count > DEVICE_LIMIT)
    count = DEVICE_LIMIT;

  s_devices = calloc(count, sizeof(Device));
  if (!s_devices)
    return false;

  s_capacity = count;
  return true;
}

void devices_free(void) {
  if (s_devices)
    free(s_devices);
  s_devices = NULL;
  s_capacity = 0;
  s_count = 0;
  s_states_at = 0;
}

int devices_state_age(void) {
  if (s_states_at == 0)
    return -1;
  return (int)(time(NULL) - s_states_at);
}

int devices_count(void) {
  return s_count;
}

int devices_capacity(void) {
  return s_capacity;
}

Device *device_at(int index) {
  if (!s_devices || index < 0 || index >= s_count)
    return NULL;
  return &s_devices[index];
}

Device *device_by_id(int32_t id) {
  for (int i = 0; i < s_count; i++)
    if (s_devices[i].id == id)
      return &s_devices[i];
  return NULL;
}

// Copies characters up to the next separator into buf, reports which separator
// ended the field in stop ('\0' at the end of the payload), and returns the
// start of the next field. The source is never modified - it lives in the
// AppMessage dictionary.
static const char *take(const char *p, char *buf, size_t len, char *stop) {
  size_t n = 0;
  while (*p && *p != FIELD_SEP && *p != REC_SEP) {
    if (n + 1 < len)
      buf[n++] = *p;
    p++;
  }
  buf[n] = '\0';
  *stop = *p;
  return *p ? p + 1 : p;
}

static uint8_t kind_from_char(char c) {
  switch (c) {
    case 'S': return KindSwitch;
    case 'D': return KindDimmer;
    case 'V': return KindVolume;
    case 'L': return KindLock;
    case 'O': return KindDoor;
    case 'W': return KindShade;
    case 'B': return KindButton;
    default: return KindSensor;
  }
}

static uint8_t value_from_char(char c) {
  switch (c) {
    case '0': return ValOff;
    case '1': return ValOn;
    case 'c': return ValClosed;
    case 'o': return ValOpen;
    case 'u': return ValUnlocked;
    case 'l': return ValLocked;
    default: return ValUnknown;
  }
}

static uint8_t level_from_string(const char *s) {
  if (!s[0] || s[0] == '-')
    return LEVEL_NONE;
  int v = atoi(s);
  if (v < 0)
    v = 0;
  if (v > 100)
    v = 100;
  return (uint8_t)v;
}

void devices_parse_list_chunk(int start_index, const char *payload) {
  if (!s_devices || !payload)
    return;

  char buf[16];
  char stop = 0;
  const char *p = payload;
  int idx = start_index;

  while (*p && idx >= 0 && idx < s_capacity) {
    Device *d = &s_devices[idx];
    memset(d, 0, sizeof(*d));

    p = take(p, buf, sizeof(buf), &stop);
    d->id = (int32_t)atoi(buf);
    if (stop != FIELD_SEP)
      break;

    p = take(p, d->name, sizeof(d->name), &stop);
    if (stop != FIELD_SEP)
      break;

    p = take(p, buf, sizeof(buf), &stop);
    d->kind = kind_from_char(buf[0]);
    if (stop != FIELD_SEP)
      break;

    p = take(p, buf, sizeof(buf), &stop);
    d->value = value_from_char(buf[0]);
    if (stop != FIELD_SEP)
      break;

    p = take(p, buf, sizeof(buf), &stop);
    d->level = level_from_string(buf);
    if (stop != FIELD_SEP)
      break;

    p = take(p, d->detail, sizeof(d->detail), &stop);
    idx++;

    if (stop != REC_SEP)
      break;
  }

  // Chunks arrive in order, so the table grows to the highest index written.
  if (idx > s_count)
    s_count = idx;

  // List records carry readings too, so this counts as a refresh.
  if (idx > start_index)
    s_states_at = time(NULL);
}

void devices_parse_state_chunk(const char *payload) {
  if (!s_devices || !payload)
    return;

  char buf[16];
  char stop = 0;
  const char *p = payload;

  while (*p) {
    p = take(p, buf, sizeof(buf), &stop);
    Device *d = device_by_id((int32_t)atoi(buf));
    if (stop != FIELD_SEP)
      break;

    p = take(p, buf, sizeof(buf), &stop);
    const uint8_t value = value_from_char(buf[0]);
    if (stop != FIELD_SEP)
      break;

    p = take(p, buf, sizeof(buf), &stop);
    const uint8_t level = level_from_string(buf);
    if (stop != FIELD_SEP)
      break;

    char detail[DEVICE_DETAIL_LEN];
    p = take(p, detail, sizeof(detail), &stop);

    // An unknown id is a device dropped since the list was cached; skip it
    // rather than resizing the table behind the menu's back.
    if (d) {
      d->value = value;
      d->level = level;
      memcpy(d->detail, detail, sizeof(detail));
    }

    if (stop != REC_SEP)
      break;
  }

  s_states_at = time(NULL);
}

bool device_is_controllable(const Device *d) {
  return d && d->kind != KindSensor;
}

bool device_has_level(const Device *d) {
  if (!d)
    return false;
  return d->kind == KindDimmer || d->kind == KindVolume || d->kind == KindShade;
}

DeviceValue device_on_value(const Device *d) {
  switch (d->kind) {
    case KindLock: return ValLocked;
    case KindDoor:
    case KindShade: return ValOpen;
    default: return ValOn;
  }
}

DeviceValue device_off_value(const Device *d) {
  switch (d->kind) {
    case KindLock: return ValUnlocked;
    case KindDoor:
    case KindShade: return ValClosed;
    default: return ValOff;
  }
}

bool device_is_on(const Device *d) {
  return d && d->value == device_on_value(d);
}

void device_value_text(const Device *d, char *out, size_t len) {
  const char *text = "--";

  switch (d->value) {
    case ValOn: text = "ON"; break;
    case ValOff: text = "OFF"; break;
    case ValOpen: text = "OPEN"; break;
    case ValClosed: text = "CLOSED"; break;
    case ValLocked: text = "LOCKED"; break;
    case ValUnlocked: text = "UNLOCKED"; break;
    default: break;
  }

  // A sensor has no on/off value at all; its reading is the headline instead.
  if (d->kind == KindSensor)
    text = d->detail[0] ? d->detail : "--";
  else if (d->kind == KindButton && d->value == ValUnknown)
    text = "PUSH";

  snprintf(out, len, "%s", text);
}

void device_status_text(const Device *d, char *out, size_t len) {
  if (d->kind == KindSensor) {
    snprintf(out, len, "%s", d->detail[0] ? d->detail : "--");
    return;
  }

  if (d->kind == KindButton) {
    snprintf(out, len, "Momentary");
    return;
  }

  char value[12];
  device_value_text(d, value, sizeof(value));

  // Capitalised words read badly in a subtitle, so only the first letter stays.
  for (size_t i = 1; value[i]; i++)
    if (value[i] >= 'A' && value[i] <= 'Z')
      value[i] = (char)(value[i] - 'A' + 'a');

  if (device_has_level(d) && d->level != LEVEL_NONE)
    snprintf(out, len, "%s - %d%%", value, (int)d->level);
  else
    snprintf(out, len, "%s", value);
}

const char *device_kind_name(const Device *d) {
  switch (d->kind) {
    case KindSwitch: return "Switch";
    case KindDimmer: return "Dimmer";
    case KindVolume: return "Volume";
    case KindLock: return "Lock";
    case KindDoor: return "Door";
    case KindShade: return "Shade";
    case KindButton: return "Button";
    default: return "Sensor";
  }
}
