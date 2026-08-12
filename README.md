# Hubitat Control

A Pebble watch app for driving [Hubitat](https://hubitat.com) devices through
the hub's **Maker API** app — lights, dimmers, speakers, locks, garage doors and
shades — either over the local network or through Hubitat's cloud relay.

Runs on every Pebble, old and new:

| Platform | Watch | Screen | |
| --- | --- | --- | --- |
| `aplite` | Classic / Steel | 144×168 | b&w |
| `basalt` | Time / Time Steel | 144×168 | colour |
| `chalk` | Time Round | 180×180 round | colour |
| `diorite` | Pebble 2 | 144×168 | b&w |
| `emery` | Time 2 | 200×228 | colour |
| `flint` | **Pebble 2 Duo** | 144×168 | b&w |
| `gabbro` | **Round 2** | 260×260 round | colour |

---

## Setting it up

### 1. Expose your devices in Hubitat

1. In the hub's web interface go to **Apps → Add Built-In App → Maker API**.
2. Under **Select Devices**, pick every device you want on the watch.
3. Turn on **Allow Access via Local IP Address** and, if you want to control
   things from away, **Allow Access via Cloud**.
4. Click **Done**, then reopen the app. Near the bottom it lists example URLs.
   The two that matter are both labelled *Get All Devices*:

   ```
   http://192.168.1.10/apps/api/34/devices?access_token=xxxxxxxx-xxxx-...
   https://cloud.hubitat.com/api/<hub-id>/apps/34/devices?access_token=xxxxxxxx-xxxx-...
   ```

### 2. Configure the watch app

Open the app's settings from the Pebble phone app and paste those two URLs into
**Local URL** and **Cloud URL**, exactly as the hub printed them. The base
address, app id and access token are read out of them, so there is nothing to
assemble by hand. Then choose a starting **Control Mode** and save.

The access token is the same for both URLs and it is a password for every device
you selected — treat the settings page accordingly.

---

## Using it

### Main menu

| Row | What it does |
| --- | --- |
| **Devices** | Opens the device list. The subtitle shows how many were found. |
| **Refresh Devices** | Discards the cached list and re-reads the hub. Use this after adding a device in Hubitat. |
| **Connection** | Toggles **Local** ⇄ **Cloud** without going back to the phone settings. |
| **Status** | The last thing the link did, or the error if something failed. Select re-reads all states. |

### Device list

* **Up / Down** — move through the list.
* **Select** — open the device.
* **Hold Select** — toggle a switch, lock or door in place, without opening it.
* **Back** — leave the app.

Each row shows its current state: `On - 75%`, `Locked`, `Closed`, `72°`.

### Device screen

| Button | Switch / Lock / Door | Dimmer / Volume / Shade | Sensor |
| --- | --- | --- | --- |
| **Select** | Toggle on ⇄ off | Toggle on ⇄ off | Re-read now |
| **Up** | Turn on / lock / open | Raise the level | — |
| **Down** | Turn off / unlock / close | Lower the level | — |

The step size for Up and Down is set on the settings page (1–25%, default 5%).
Holding the button repeats. A level is sent once you stop moving it, so dragging
a dimmer from 10% to 80% is one command to the hub, not fourteen.

Momentary buttons take a single **Select** press to fire, with a short buzz to
confirm.

---

## What gets fetched, and when

The requirement this app is built around: the hub is asked for its device list
**once**, and that list is then reused.

* **First launch** — the phone reads `/devices/all`, works out what each device
  is, sends the list to the watch, and caches it.
* **Later launches** — the cached list goes to the watch immediately, so the menu
  is up before the network is even touched. States are then read fresh from
  `/devices/all` and filled in.
* **The cache is discarded** when you choose **Refresh Devices**, when the
  settings page is saved, or when the hub/token in the settings changes.
* **States are never cached.** Every launch reads them from the hub, and opening
  a device screen re-reads that one device.

A device added in Hubitat after the list was cached will not appear until you
refresh — that is the point of freezing the list, not a bug.

---

## Devices it understands

Classified from the Maker API's reported capabilities, most specific first:

| Capability | Shown as | Select | Up / Down |
| --- | --- | --- | --- |
| `Lock` | Lock | `lock` / `unlock` | lock / unlock |
| `GarageDoorControl`, `DoorControl` | Door | `open` / `close` | open / close |
| `WindowShade` | Shade | `open` / `close` | `setPosition` |
| `AudioVolume`, `MusicPlayer` | Volume | `on`/`off`, or `mute`/`unmute` when there is no switch | `setVolume` |
| `SwitchLevel` | Dimmer | `on` / `off` | `setLevel` |
| `Switch` | Switch | `on` / `off` | on / off |
| `Momentary`, `PushableButton` | Button | `push` | — |
| anything else | Sensor | re-read | — |

Read-only devices show their most interesting reading — temperature, motion,
contact, presence, humidity, power, battery and a few more, in that order. They
can be hidden entirely from the settings page.

---

## How it is put together

The watch holds no credentials and makes no HTTP calls. It sends an operation
code over AppMessage; the phone-side JavaScript owns the hub connection.

```
src/c/
  main.c            app lifecycle, routes change notifications to windows
  comm.c/.h         AppMessage transport, outbound request queue, link state
  devices.c/.h      the device table and the wire-format parser
  main_menu.c/.h    devices / refresh / connection / status
  device_list.c/.h  the scrolling device list
  device_window.c/.h  one device's control screen
  theme.h           colours, with monochrome fallbacks for aplite and diorite
src/pkjs/
  index.js          Maker API client, device classification, list cache, chunking
  config.js         Clay settings page
```

**The wire format.** Devices cross in chunks of records — fields separated by
`\x1f`, records by `\x1e` — sized to whatever inbox the watch managed to
allocate, which it reports at handshake along with how many rows it can hold
(48 on aplite, 160 on emery, 120 on the rest). Both separators are stripped from
device names, so they cannot appear in the data.

**Layout is derived from the bounds, not the platform.** Font sizes come from a
three-way tier on screen width, and the round insets scale with the display, so
gabbro's 260 px circle keeps its content inside the glass where fixed values
tuned for chalk would not. The kind label on the control screen is drawn only
where there is room left for it — on a 180 px circle showing a level bar there
is none, so chalk drops it rather than overprinting the percentage.

**Commands are semantic.** The watch only ever sends `on`, `off`, `level` or
`push`; the phone translates each into the Maker API command that particular
device accepts. Adding a new device type is a change to `index.js` and one enum
in `devices.h`, not to every button handler.

**Optimism, then correction.** A press updates the watch's copy immediately so
the screen never feels laggy, then the phone reads the device back once it has
settled — a second for a light, three for a lock — and pushes the real state.

---

## Away from home, and your token

The Maker API access token is a key to every device you exposed. In **Local**
mode it is sent to a private address like `192.168.1.10` — and on a café, hotel
or airport network that address very often belongs to somebody else's machine.
Sending it there would hand a stranger control of your home.

So Local mode has to prove it is on the right network first. Before any token
moves, the app makes one **token-free** request to the hub's address. A real hub
identifies itself by refusing an unauthenticated caller (`401`/`403`) or by
answering JSON. Silence, a timeout, or a captive portal answering with HTML all
count as *not home*, and the token is never sent.

What happens when the probe says you are away depends on two settings:

| Cloud access | VPN | Behaviour away from home |
| --- | --- | --- |
| Yes | either | Falls back to Cloud automatically — `Away - using Cloud` |
| No | Yes | Stops, and says `Not home - connect VPN` |
| No | No | Stops, and says `Not on home network` |

In the last two cases it genuinely stops: the verdict is cached, so no further
request is attempted — **the app does not poll a network it should not be on.**
Refresh Devices, switching Local/Cloud, or saving settings all re-check
immediately, so walking back in the door costs one button press.

With **Cloud Access Available** turned off the app is Local-only: the watch's
Connection row reads `Local only` and will not switch, rather than offering a
route that cannot work.

**The limits, honestly.** The probe proves *something at that address behaves
like a hub*, not that it is yours. A hostile access point that deliberately
answers JSON on that path could still pass it. It removes the everyday risk —
a stranger's NAS or router quietly receiving your token — rather than defeating
a targeted attacker. If that matters to you, turn Cloud on and leave Local for
home, or use a VPN. **Verify Hub Before Sending Token** can be turned off if it
misbehaves on an unusual setup, but it is the safeguard, so leave it on.

### The URL itself is checked

A token is never sent in clear text anywhere it could leave your network. A URL
is refused outright — with the reason shown on the watch — when it is:

* `http://` to anything that is not a private address. `10.x`, `172.16–31.x`,
  `192.168.x`, `127.x`, `169.254.x`, a bare hostname, or a `.local` / `.lan` /
  `.home` / `.internal` name are accepted; a public IP or a real domain is not.
* missing its `access_token`, or not `http(s)` at all.

So pasting the cloud URL as `http://` rather than `https://`, or pointing the
local URL at a public address, fails closed instead of quietly leaking the key
to your house. `https://` to anywhere is fine — that is the cloud path.

### Locks and doors

Holding select on the device list toggles a device without opening it. Locks,
garage doors and door controls are **left out of that shortcut**: a long press
is easy to make by accident, and unlocking a front door is the one action here
with a physical consequence. Holding select on one opens its screen instead, so
the press still does something useful and nothing irreversible.

**Allow Locks And Doors In Shortcut** turns that off if you want it. The setting
says plainly what it means: one long press on a list row can then unlock your
door or open your garage with nothing to confirm it.

### What this cannot protect against

* **Local mode is plain HTTP.** Hubitat's Maker API offers no local TLS, so on
  your own LAN the token travels in clear text and anything else on that network
  — including a compromised smart plug — could read it. This is a property of
  the hub's API, not of this app. If you suspect exposure, regenerate the token
  in the Maker API app; every URL changes with it, and re-saving the settings
  here is enough to pick it up.
* **The settings page shows your token**, because it holds the URL you pasted.
  Anyone with your unlocked phone can read it. Masking it would remove your
  ability to check what you pasted, which seemed the worse trade.
* **The cached device list** in the phone's storage holds device names, which
  describe your home. No credentials, but it is not nothing.

The watch itself never holds the token, never makes an HTTP request, and
persists only which mode you last chose.

---

## Battery

The app is entirely event-driven. **Nothing runs in the background on either
device**: there is no tick timer, no health, accelerometer or compass
subscription, no `setInterval`, and PebbleKit JS is torn down with the watch app,
so a closed app costs nothing at all. Sitting on an open menu, no timer is
scheduled and no request is in flight.

All cost is per-session:

| Action | Cost |
| --- | --- |
| Launch | one `/devices/all`, plus the list and states over AppMessage |
| Open a device | one `/devices/<id>` — skipped if the table was refreshed in the last 20 s |
| Send a command | the command, plus one confirming read after it settles |
| Drag a dimmer | **one** command — the level is debounced 450 ms, not sent per step |
| Idle | nothing |

Failure paths are where a home-control app usually bleeds battery, so both are
capped:

* **Watch out of range.** The outbox stops dead rather than retrying into a dead
  link, and the connection service — itself event-driven — resumes it on
  reconnect. Requests stay queued meanwhile.
* **Phone without signal, or in airplane mode.** The watch link is fine but every
  hub request is doomed, and each one otherwise holds the radio until it times
  out. Local requests time out in 6 s (a hub on the LAN answers in well under
  one), and after three consecutive transport failures the client stops trying
  for 60 s, failing instantly with no radio use. **Refresh Devices**, switching
  Local/Cloud, or saving settings all clear that immediately, so you are never
  stuck waiting out a cooldown.
* An HTTP error such as 401 or 404 means the hub *answered* — a settings problem,
  not a signal one — so it never trips the backoff and stays visible.

Being away from home with **Local** selected is the common version of this, and
the watch says so: it reports `Hub unreachable - try Cloud`, which the Connection
row fixes without touching the phone.

---

## Building

There is no Pebble SDK on this machine; build on
[CloudPebble](https://cloudpebble.repebble.com/). Zip this folder with itself as
the single top-level directory, then **Projects → Import → Upload Zip** — that
reads `package.json`, so the platform list, message keys, Clay dependency and
UUID all arrive already set.

---

## Notes

An unofficial community app. Not affiliated with or endorsed by Hubitat, Inc.
See [NOTICE](NOTICE).
