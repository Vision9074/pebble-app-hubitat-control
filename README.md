# Hubitat Control

Control your [Hubitat](https://hubitat.com) home from your wrist.

A Pebble watch app for lights, dimmers, speakers, locks, garage doors, shades and
sensors, talking to your hub through its built-in **Maker API** app — over your
home network, or through Hubitat's cloud relay when you are out.

No account, no third-party service, no bridge to install. Your hub and your phone
are the only things involved.

- [Features](#features)
- [Supported watches](#supported-watches)
- [What you need](#what-you-need)
- [Setting it up](#setting-it-up)
- [Using it](#using-it)
- [Settings](#settings)
- [Devices it understands](#devices-it-understands)
- [Security](#security)
- [Battery](#battery)
- [Troubleshooting](#troubleshooting)
- [How it works](#how-it-works)
- [Building from source](#building-from-source)
- [Licence and trademarks](#licence-and-trademarks)

---

## Features

- **Every device on one list**, with its current state — `On · 75%`, `Locked`,
  `Closed`, `72°`.
- **One-button control.** Select toggles; up and down drive dimmers, volume and
  shade position.
- **Local or Cloud**, switchable from the watch itself — no digging through phone
  settings when you leave the house.
- **Fast.** The device list is cached, so the menu is on screen before the
  network is touched. Live states load in behind it.
- **Careful with your credentials.** The watch never holds your access token, and
  the app refuses to send it to a network that has not proven it is your own.
- **Careful with your battery.** Nothing polls, nothing runs in the background.

---

## Supported watches

Every Pebble ever made, including the new ones.

| Watch | Screen | |
| --- | --- | --- |
| Pebble Classic / Steel | 144×168 | black & white |
| Pebble Time / Time Steel | 144×168 | colour |
| Pebble Time Round | 180×180 round | colour |
| Pebble 2 | 144×168 | black & white |
| Pebble Time 2 | 200×228 | colour |
| Pebble 2 Duo | 144×168 | black & white |
| Pebble Round 2 | 260×260 round | colour |

---

## What you need

- A Hubitat Elevation hub on your network.
- A Pebble watch, paired to the Pebble phone app.
- Five minutes to turn on Maker API and paste two URLs.

---

## Setting it up

### 1. Expose your devices in Hubitat

1. In the hub's web interface, go to **Apps → Add Built-In App → Maker API**.
2. Under **Select Devices**, pick every device you want on the watch. Only these
   are ever visible to the app.
3. Turn on **Allow Access via Local IP Address**. If you want control while away
   from home, also turn on **Allow Access via Cloud**.
4. Click **Done**, then reopen the app. Near the bottom it lists example URLs.
   The two you want are both labelled *Get All Devices*:

   ```
   http://192.168.1.10/apps/api/34/devices?access_token=xxxxxxxx-xxxx-…
   https://cloud.hubitat.com/api/<hub-id>/apps/34/devices?access_token=xxxxxxxx-xxxx-…
   ```

### 2. Configure the watch app

Open the app's settings from the Pebble phone app and paste those two URLs into
**Local URL** and **Cloud URL**, exactly as the hub printed them. The address,
app id and access token are read out of them — there is nothing to assemble by
hand.

Then set **Cloud Access Available** to match what you turned on in step 3, pick a
starting **Control Mode**, and save.

> **Your access token is a password for every device you selected.** Anyone who
> has it can unlock what it can unlock. Treat the settings page like a password
> field, and see [Security](#security) for what the app does to protect it.

---

## Using it

### Main menu

| Row | What it does |
| --- | --- |
| **Devices** | Opens the device list. The subtitle shows how many were found. |
| **Refresh Devices** | Re-reads the hub from scratch. Use after adding a device in Hubitat, or to retry after an error. |
| **Connection** | Switches **Local** ⇄ **Cloud**. Reads `Local only` when no cloud route is configured. |
| **Status** | What the link last did, or why it failed. Select re-reads all states. |

### Device list

| Button | Action |
| --- | --- |
| **Up / Down** | Move through the list |
| **Select** | Open the device |
| **Hold Select** | Toggle a switch, plug or light **without opening it** |
| **Back** | Leave the app |

Hold-to-toggle deliberately does **not** act on locks, garage doors or door
controls — holding select on one of those opens its screen instead, so unlocking
is always something you chose to do. See [Locks and doors](#locks-and-doors).

### Device screen

| Button | Switch / Lock / Door | Dimmer / Volume / Shade | Sensor |
| --- | --- | --- | --- |
| **Select** | Toggle on ⇄ off | Toggle on ⇄ off | Re-read now |
| **Up** | Turn on / lock / open | Raise the level | — |
| **Down** | Turn off / unlock / close | Lower the level | — |

Holding up or down repeats. The level is sent once you stop moving it, so
dragging a dimmer from 10% to 80% is **one** command to your hub, not fourteen.

Momentary buttons fire on a single **Select**, with a short buzz to confirm.

---

## Settings

All of these live on the app's settings page in the Pebble phone app.

### Hub

| Setting | Default | What it does |
| --- | --- | --- |
| **Local URL** | — | The `http://` example URL from Maker API, with your hub's IP |
| **Cloud URL** | — | The `https://cloud.hubitat.com` example URL |
| **Control Mode** | Local | Which route to start on |

### Device list

| Setting | Default | What it does |
| --- | --- | --- |
| **Show Read-Only Devices** | On | Include sensors and meters, which display a reading but take no commands |
| **Order** | By name | By name, by type then name, or the hub's own order |
| **Dimmer / Volume Step** | 5% | How far one press of up or down moves a level (1–25%) |

### Away from home

| Setting | Default | What it does |
| --- | --- | --- |
| **Cloud Access Available** | On | Turn off if your hub has no cloud access. The app becomes Local-only. |
| **VPN To Home Network** | Off | Turn on if you use a VPN home, so Local is expected to work while away |
| **Verify Hub Before Sending Token** | On | The safeguard described in [Security](#security). Leave it on. |

### Locks and doors

| Setting | Default | What it does |
| --- | --- | --- |
| **Allow Locks And Doors In Shortcut** | Off | Lets hold-to-toggle unlock doors and open garages. Read the warning first. |

---

## Devices it understands

Worked out from the capabilities your hub reports, most specific first.

| Capability | Shown as | Select | Up / Down |
| --- | --- | --- | --- |
| `Lock` | Lock | lock / unlock | lock / unlock |
| `GarageDoorControl`, `DoorControl` | Door | open / close | open / close |
| `WindowShade` | Shade | open / close | set position |
| `AudioVolume`, `MusicPlayer` | Volume | on/off, or mute/unmute if it has no switch | set volume |
| `SwitchLevel` | Dimmer | on / off | set level |
| `Switch` | Switch | on / off | on / off |
| `Momentary`, `PushableButton` | Button | push | — |
| anything else | Sensor | re-read | — |

Read-only devices show their most interesting reading — temperature, motion,
contact, presence, humidity, illuminance, power, battery and a few more, in that
order. Turn off **Show Read-Only Devices** to leave them out entirely.

### What gets fetched, and when

The hub is asked for its device *list* once, and that list is then reused:

- **First launch** reads every device, works out what each one is, sends the list
  to the watch and caches it.
- **Later launches** send the cached list straight to the watch, so the menu is up
  before the network is touched — then read live states and fill them in.
- **The cache is dropped** when you choose Refresh Devices, save the settings, or
  change hub or token.
- **States are never cached.** They are read fresh every launch.

A device added in Hubitat after the list was cached will not appear until you
choose **Refresh Devices**. That is the design, not a bug.

---

## Security

### Your token, and public networks

The Maker API access token is a key to every device you exposed. In **Local**
mode it is sent to a private address like `192.168.1.10` — and on a café, hotel
or airport network, that address very often belongs to somebody else's machine.

So Local mode has to prove it is on the right network first. Before any token
moves, the app makes one **token-free** request to the hub's address. A real hub
identifies itself by refusing an unauthenticated caller (`401`/`403`) or by
answering JSON. Silence, a timeout, or a captive portal answering with HTML all
count as *not home*, and your token is never sent.

What happens then depends on your settings:

| Cloud access | VPN | Away from home |
| --- | --- | --- |
| Yes | either | Falls back to Cloud automatically — `Away - using Cloud` |
| No | Yes | Stops: `Not home - connect VPN` |
| No | No | Stops: `Not on home network` |

In the last two it genuinely stops — the verdict is cached, so nothing further is
attempted. **The app will not poll a network it should not be on.** Refresh
Devices, switching Local/Cloud, or saving settings all re-check immediately, so
walking back in the door costs one button press.

### The URL itself is checked

A token is never sent in clear text anywhere it could leave your network. A URL
is refused outright, with the reason shown on the watch, when it is:

- `http://` to anything that is not a private address. Accepted: `10.x`,
  `172.16–31.x`, `192.168.x`, `127.x`, `169.254.x`, a bare hostname, or a
  `.local` / `.lan` / `.home` / `.internal` name. A public IP or a real domain is
  not.
- missing its `access_token`, or not `http(s)` at all.

So pasting the cloud URL as `http://` instead of `https://`, or pointing the
local URL at a public address, fails closed rather than quietly leaking the key
to your house.

### Locks and doors

Hold-to-toggle on the device list does not act on locks, garage doors or door
controls. A long press is easy to make by accident, and unlocking a front door is
the one action here with a physical consequence — so holding select on one opens
its screen instead. The press still does something useful, and nothing
irreversible.

**Allow Locks And Doors In Shortcut** turns that off if you want it. Be clear
about what you are choosing: one long press on a list row can then unlock your
door or open your garage, with nothing to confirm it.

### What this cannot protect against

Stated plainly, because security features that oversell themselves are worse than
none:

- **The probe proves something at that address behaves like a hub — not that it
  is yours.** A hostile access point that deliberately answers JSON could still
  pass it. This removes the everyday risk of a stranger's router or NAS quietly
  receiving your token; it does not defeat a targeted attacker. If that is your
  threat model, use Cloud away from home, or a VPN.
- **Local mode is plain HTTP.** Hubitat's Maker API offers no local TLS, so on
  your own network the token travels in clear text and anything else on that
  network could read it. This is a property of the hub's API, not of this app.
  If you suspect exposure, regenerate the token in the Maker API app and re-save
  your settings here.
- **The settings page shows your token**, because it holds the URL you pasted.
  Anyone with your unlocked phone can read it.
- **The cached device list** on your phone holds device names, which describe your
  home. No credentials, but not nothing.

The watch itself never holds your token, never makes a network request, and
remembers only which mode you last chose.

---

## Battery

The app is entirely event-driven. **Nothing runs in the background on either
device** — no timers, no sensor subscriptions, no polling — and the phone-side
code is shut down with the watch app. A closed app costs nothing at all, and
sitting on an open menu, nothing is scheduled and nothing is in flight.

| Action | Cost |
| --- | --- |
| Launch | one read of all devices, plus the list and states to the watch |
| Open a device | one read — skipped if states were refreshed in the last 20 s |
| Send a command | the command, plus one confirming read once it settles |
| Drag a dimmer | **one** command, not one per step |
| Idle | nothing |

Failure paths are where a home-control app usually drains a battery, so both are
capped:

- **Watch out of range of the phone.** The watch stops transmitting rather than
  retrying into a dead link, and resumes automatically on reconnect. Anything you
  pressed stays queued.
- **Phone with no signal, or in airplane mode.** Local requests give up after 6
  seconds, and after three failures in a row the app stops trying for a minute,
  failing instantly with no radio use at all. Refresh Devices, switching mode, or
  saving settings clears that immediately.
- A hub that answers with an error (a wrong token, say) is a settings problem, not
  a signal one, so it never trips the backoff and stays visible.

---

## Troubleshooting

| The watch says | What it means |
| --- | --- |
| `Set the local URL` / `Set the cloud URL` | Nothing pasted for the current mode. Check the settings page. |
| `URL has no access token` | The URL was pasted without its `?access_token=…` part. Copy the whole line from Maker API. |
| `Refusing HTTP to a public address` | A `http://` URL pointing somewhere off your network. Cloud URLs must be `https://`. |
| `Not on home network` | You are away, and there is no cloud route configured. Turn on **Cloud Access Available** and add a Cloud URL. |
| `Not home - connect VPN` | You are away with VPN selected — connect the VPN, then choose Refresh Devices. |
| `Hub unreachable - try Cloud` | Local could not reach the hub. Use the **Connection** row to switch to Cloud. |
| `Phone disconnected` | The watch has lost Bluetooth. It resumes on its own when the phone is back. |
| `No devices exposed` | Maker API has no devices selected, or **Show Read-Only Devices** is off and everything you exposed is a sensor. |
| `Local only` on the Connection row | No usable Cloud URL, or **Cloud Access Available** is off. |
| `Cloud access is turned off` | You tried to switch to Cloud with that setting off. |

**A device is missing.** The list is cached on purpose. Choose **Refresh
Devices** after adding anything in Hubitat.

**A device shows the wrong state.** Choose **Status → Select** to re-read
everything, or open the device to re-read just that one.

**Everything is slow on a big hub.** Every launch reads all device states. Turn
off **Show Read-Only Devices** to cut the list down to things you can act on.

---

## How it works

The watch holds no credentials and makes no network calls. It sends an operation
code over Bluetooth; the phone-side JavaScript owns the hub connection.

```
src/c/
  main.c              app lifecycle, routes updates to the open windows
  comm.c/.h           AppMessage transport, request queue, link state
  devices.c/.h        the device table and the wire-format parser
  main_menu.c/.h      devices / refresh / connection / status
  device_list.c/.h    the scrolling device list
  device_window.c/.h  one device's control screen
  theme.h             colours, with monochrome fallbacks
src/pkjs/
  index.js            Maker API client, device classification, cache, safeguards
  config.js           settings page
```

**Commands are semantic.** The watch only ever sends `on`, `off`, `level` or
`push`. The phone translates each into the Maker API command that particular
device accepts, so adding a device type touches the JavaScript and one enum, not
every button handler.

**Optimism, then correction.** A press updates the watch immediately so the
screen never feels laggy; the phone then reads the device back once it has
settled — a second for a light, three for a lock — and pushes the real state.

**Layout comes from the screen bounds, not the platform.** Font sizes step with
screen width and round insets scale with the display, so a 260 px circle and a
144 px rectangle are both laid out correctly without per-model special cases.

**The wire format.** Devices cross in chunked records sized to whatever message
buffer the watch could allocate, which it reports at handshake along with how
many rows it can hold — 48 on the oldest hardware, 160 on the largest.

---

## Building from source

With the [Pebble SDK](https://developer.repebble.com/):

```bash
pebble build
pebble install --emulator emery
```

Or on [CloudPebble](https://cloudpebble.repebble.com/): zip this folder with
itself as the single top-level directory, then **Projects → Import → Upload
Zip**. That reads `package.json`, so the platform list, message keys,
dependencies and UUID all arrive already set.

---

## Licence and trademarks

An unofficial community app, not affiliated with, sponsored by, or endorsed by
Hubitat, Inc. "Hubitat" and "Hubitat Elevation" are their trademarks, used here
only to identify the hub this app talks to. No Hubitat branding is reproduced.

The app contains no Hubitat code and embeds no credentials — it talks only to the
Maker API instance you set up yourself, with a token you supply.

See [NOTICE](NOTICE) for full attribution.
