// The device table. Filled from chunks the phone pushes over AppMessage and
// read by every window; nothing here talks to the hub directly.

#pragma once

#include <pebble.h>

// Labels are truncated on the phone to fit, so the watch never has to wrap.
#define DEVICE_NAME_LEN 26
#define DEVICE_DETAIL_LEN 14

// A level of LEVEL_NONE means the device has no dimmer/volume/position value.
#define LEVEL_NONE 255

// Aplite has 24 KB of app RAM; the others have far more. The phone is told this
// ceiling at handshake and truncates its list to match. Basalt, chalk, diorite,
// flint and gabbro all take the middle figure - at 48 bytes a row that is under
// 6 KB, comfortable on every one of them.
#if defined(PBL_PLATFORM_APLITE)
#define DEVICE_LIMIT 48
#elif defined(PBL_PLATFORM_EMERY)
#define DEVICE_LIMIT 160
#else
#define DEVICE_LIMIT 120
#endif

typedef enum {
  KindSensor = 0,  // read-only; shows a value, takes no commands
  KindSwitch,
  KindDimmer,
  KindVolume,
  KindLock,
  KindDoor,
  KindShade,
  KindButton
} DeviceKind;

typedef enum {
  ValUnknown = 0,
  ValOff,
  ValOn,
  ValClosed,
  ValOpen,
  ValUnlocked,
  ValLocked
} DeviceValue;

typedef struct {
  int32_t id;
  uint8_t kind;
  uint8_t value;
  uint8_t level;
  char name[DEVICE_NAME_LEN];
  char detail[DEVICE_DETAIL_LEN];
} Device;

// Allocates room for count devices (capped at DEVICE_LIMIT) and empties the
// table. Returns false if the allocation failed.
bool devices_reserve(int count);
void devices_free(void);

int devices_count(void);
int devices_capacity(void);
Device *device_at(int index);
Device *device_by_id(int32_t id);

// Both take a payload of records separated by \x1e, fields by \x1f.
// List records:  id, name, kind, value, level, detail
// State records: id, value, level, detail
void devices_parse_list_chunk(int start_index, const char *payload);
void devices_parse_state_chunk(const char *payload);

// Seconds since the table last took a set of readings, or -1 if it never has.
// Lets a caller skip a re-read the launch refresh already covered.
int devices_state_age(void);

bool device_is_controllable(const Device *d);
bool device_has_level(const Device *d);

// The value this device takes when switched on or off, which is not always
// ValOn/ValOff - a lock reads Locked/Unlocked, a door Open/Closed.
DeviceValue device_on_value(const Device *d);
DeviceValue device_off_value(const Device *d);
bool device_is_on(const Device *d);

// One-line summary for a menu row, e.g. "On - 75%" or "Locked".
void device_status_text(const Device *d, char *out, size_t len);
// The big word on the control screen, e.g. "ON", "LOCKED".
void device_value_text(const Device *d, char *out, size_t len);
const char *device_kind_name(const Device *d);
