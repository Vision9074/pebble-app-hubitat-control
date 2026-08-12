// Phone side of Hubitat Control. Everything that touches the hub lives here:
// the watch only ever sends an operation code and reads back a device table.
//
// Local mode talks straight to the hub on the LAN; Cloud mode goes through
// cloud.hubitat.com. Both use the same Maker API access token, so the settings
// page just takes the two example URLs the Maker API app prints.

var Clay = require('@rebble/clay');
var clayConfig = require('./config');

// Clay's own handler would push every setting - including two long URLs - at
// the watch, which will not fit a small inbox. Only what the watch needs is
// sent, from the webviewclosed handler below.
var clay = new Clay(clayConfig, null, { autoHandleEvents: false });

// Watch -> phone operations.
var OP_HELLO = 0;
var OP_LIST = 1;
var OP_REFRESH = 2;
var OP_STATES = 3;
var OP_COMMAND = 4;
var OP_MODE = 5;
var OP_DEVICE = 6;

// Phone -> watch messages.
var MSG_READY = 0;
var MSG_LIST_CHUNK = 1;
var MSG_STATE_CHUNK = 2;
var MSG_STATUS = 3;
var MSG_LIST_BEGIN = 4;
var MSG_LIST_END = 5;

var STATUS_IDLE = 0;
var STATUS_LOADING = 1;
var STATUS_ERROR = 2;

// Record and field separators inside a chunk payload. Both are stripped from
// device names, so they can never appear in the data.
var RS = '\x1e';
var FS = '\x1f';

var CACHE_KEY = 'hubitat-device-list';
var MODE_KEY = 'hubitat-mode';

var NAME_MAX = 25;
var DETAIL_MAX = 13;

// Per-request ceilings, and the backoff that stops a dead network being retried.
var LOCAL_TIMEOUT = 6000;
var CLOUD_TIMEOUT = 20000;
var FAILURE_LIMIT = 3;
var COOLDOWN_MS = 60000;

// The token-free "are we on the home network" check, and how long its verdict
// stands before it is worth asking again.
var PROBE_TIMEOUT = 3000;
var HOME_TTL_MS = 5 * 60 * 1000;

// Replaced by the real figures at handshake.
var watchInbox = 512;
var deviceLimit = 48;

var deviceList = [];
var deviceIndex = {};
var listBusy = false;
var lastListAt = 0;

// --------------------------------------------------------------- settings ---

function settings() {
  try {
    var raw = localStorage.getItem('clay-settings');
    return raw ? JSON.parse(raw) : {};
  } catch (e) {
    return {};
  }
}

function currentMode() {
  var stored = localStorage.getItem(MODE_KEY);
  if (stored === '0' || stored === '1')
    return parseInt(stored, 10);
  return settings().HubMode === 'cloud' ? 1 : 0;
}

function setMode(mode) {
  localStorage.setItem(MODE_KEY, mode ? '1' : '0');
}

function stepSize() {
  var n = parseInt(settings().HubStep, 10);
  return (n > 0 && n <= 50) ? n : 5;
}

function showSensors() {
  var s = settings();
  return s.HubShowSensors === undefined ? true : !!s.HubShowSensors;
}

function sortMode() {
  return settings().HubSort || 'name';
}

function boolSetting(name, fallback) {
  var v = settings()[name];
  return v === undefined ? fallback : !!v;
}

// Cloud is only a real option if the user has it and gave us a usable URL.
function cloudAvailable() {
  return boolSetting('HubCloudAvailable', true) &&
         !!parseEndpoint(settings().HubCloudUrl);
}

function vpnAvailable() {
  return boolSetting('HubVpn', false);
}

function verifyLocal() {
  return boolSetting('HubVerifyLocal', true);
}

// Off by default: unlocking a door from the list is one accidental long press
// away, so it normally requires opening the device deliberately.
function quickLockAllowed() {
  return boolSetting('HubQuickUnlock', false);
}

// Accepts either a bare base URL or the full example URL the Maker API app
// prints, with or without the trailing /devices and query string.
function splitUrl(raw) {
  if (!raw)
    return null;

  raw = String(raw).trim();
  if (!raw)
    return null;

  var token = '';
  var match = raw.match(/[?&]access_token=([^&\s]+)/i);
  if (match)
    token = match[1];

  var base = raw.split('?')[0].replace(/\s+$/, '');
  base = base.replace(/\/devices(\/.*)?$/i, '');
  base = base.replace(/\/+$/, '');

  return { base: base, token: token };
}

function hostOf(base) {
  var m = base.match(/^https?:\/\/([^\/:]+)/i);
  return m ? m[1].toLowerCase() : '';
}

// Addresses that cannot leave the local network, so plain HTTP to them keeps the
// token on the wire you are already trusting.
function isPrivateHost(host) {
  if (!host)
    return false;
  if (host === 'localhost' || host === '::1' || host === '[::1]')
    return true;
  if (host.indexOf('.') === -1)          // bare hostname, e.g. "hubitat"
    return true;
  if (/\.(local|lan|home|internal)$/.test(host))
    return true;

  var m = host.match(/^(\d+)\.(\d+)\.(\d+)\.(\d+)$/);
  if (!m)
    return false;

  var a = parseInt(m[1], 10);
  var b = parseInt(m[2], 10);
  return a === 10 || a === 127 ||
         (a === 192 && b === 168) ||
         (a === 172 && b >= 16 && b <= 31) ||
         (a === 169 && b === 254);
}

// Returns why a URL must not be used, or null when it is safe to send a token
// to. The rule that matters: an access token is a key to every device on the
// hub, so it never travels in clear text anywhere it could leave the LAN.
function urlProblem(raw) {
  var parts = splitUrl(raw);
  if (!parts || !parts.base)
    return 'No hub URL set';
  if (!/^https?:\/\//i.test(parts.base))
    return 'URL must start with http';
  if (!parts.token)
    return 'URL has no access token';
  if (/^http:\/\//i.test(parts.base) && !isPrivateHost(hostOf(parts.base)))
    return 'Refusing HTTP to a public address';
  return null;
}

function parseEndpoint(raw) {
  if (urlProblem(raw))
    return null;
  return splitUrl(raw);
}

// Explains the current mode's URL problem, for the watch's status line.
function endpointError() {
  var s = settings();
  var raw = currentMode() ? s.HubCloudUrl : s.HubLocalUrl;
  if (!raw || !String(raw).trim())
    return currentMode() ? 'Set the cloud URL' : 'Set the local URL';
  return urlProblem(raw) || 'Bad hub URL';
}

function endpoint() {
  var s = settings();
  return parseEndpoint(currentMode() ? s.HubCloudUrl : s.HubLocalUrl);
}

// Both modes share an access token and app id, so a list cached in one mode is
// still valid in the other. Only a changed hub or token invalidates it.
// Empty when the app is not configured, which readCache and writeCache both
// treat as "never matches" - otherwise a list cached against one hub would be
// served back after the settings were cleared.
function cacheSignature() {
  var ep = endpoint();
  if (!ep)
    return '';
  var app = ep.base.match(/\/apps(?:\/api)?\/(\d+)$/);
  return (app ? app[1] : ep.base) + '|' + ep.token;
}

// ------------------------------------------------------------ send queue ---

var outbox = [];
var sending = false;

function pump() {
  if (sending || !outbox.length)
    return;

  var item = outbox[0];
  sending = true;

  Pebble.sendAppMessage(item.msg, function () {
    sending = false;
    outbox.shift();
    pump();
  }, function (e) {
    sending = false;
    item.tries++;
    if (item.tries >= 3) {
      console.log('hubitat: giving up on a message - ' +
                  (e && e.error ? e.error.message : 'unknown'));
      outbox.shift();
    }
    setTimeout(pump, 300);
  });
}

// Chunks must arrive in order and the watch has one inbox, so nothing is sent
// until the previous message is acknowledged.
function send(msg) {
  outbox.push({ msg: msg, tries: 0 });
  pump();
}

function status(code, text) {
  send({
    Msg: MSG_STATUS,
    Num: code,
    Payload: String(text).substring(0, 40),
    Mode: currentMode(),
    Step: stepSize(),
    // Lets the watch's Connection row refuse a mode that cannot work.
    CloudOk: cloudAvailable() ? 1 : 0,
    // Whether the list's hold-to-toggle may act on locks and doors.
    QuickLock: quickLockAllowed() ? 1 : 0
  });
}

// Leaves room for the dictionary headers around the payload string.
function chunkBudget() {
  return Math.max(120, Math.min(watchInbox - 120, 600));
}

// ------------------------------------------------------------- hub client ---

// A hub on the LAN answers in well under a second, so a long local timeout only
// buys a stalled radio: if it has not replied in six seconds this phone is not
// on that network. The cloud relay is allowed considerably longer.
function requestTimeout() {
  return currentMode() ? CLOUD_TIMEOUT : LOCAL_TIMEOUT;
}

// Airplane mode, no signal, or Local mode while away from home all mean every
// request is doomed, and each one holds the radio open until it times out. After
// a few in a row the client stops trying for a cooldown and fails instantly
// instead - no radio at all - until the user asks for a refresh or it expires.
//
// Only transport failures count. An HTTP 401 or 404 means the hub answered, so
// it is a settings problem and must not trip a breaker that hides it.
var consecutiveFailures = 0;
var coolingUntil = 0;

function noteReachable() {
  consecutiveFailures = 0;
  coolingUntil = 0;
}

function noteUnreachable() {
  consecutiveFailures++;
  if (consecutiveFailures >= FAILURE_LIMIT)
    coolingUntil = Date.now() + COOLDOWN_MS;
}

// Called when the user explicitly asks for a refresh - they may have just walked
// back into range, and should never be stuck waiting out a cooldown.
function clearBackoff() {
  consecutiveFailures = 0;
  coolingUntil = 0;
}

function unreachableText() {
  // In Local mode the usual cause is being away from home, which the watch's
  // own Connection row can fix without going near the phone.
  return currentMode() ? 'No connection to cloud' : 'Hub unreachable - try Cloud';
}

// --------------------------------------------------- home network guard ---
//
// The access token is a key to every device on the hub, and in Local mode it
// travels to a private address such as 192.168.1.10. On a café or hotel network
// that address is very likely to belong to somebody else's machine, so the token
// must never be sent until something has answered there that behaves like a hub.
//
// The probe below is that check. It is deliberately sent WITHOUT the token: the
// worst it can leak to a stranger's device is the fact that a request arrived.
// A hub proves itself by refusing an unauthenticated request (401/403) or by
// answering JSON; a captive portal answering 200 with HTML does not qualify, and
// neither does silence.
//
// The verdict is cached, so being away costs one short probe and then nothing at
// all - which is what stops the app polling a network it should not be on.

var homeState = { known: false, home: false, at: 0 };

function homeVerdictFresh() {
  return homeState.known && (Date.now() - homeState.at) < HOME_TTL_MS;
}

function clearHomeVerdict() {
  homeState = { known: false, home: false, at: 0 };
}

function probeHome(done) {
  if (homeVerdictFresh()) {
    done(homeState.home);
    return;
  }

  var ep = parseEndpoint(settings().HubLocalUrl);
  if (!ep) {
    done(false);
    return;
  }

  function settle(home) {
    homeState = { known: true, home: home, at: Date.now() };
    done(home);
  }

  var xhr = new XMLHttpRequest();
  xhr.timeout = PROBE_TIMEOUT;

  xhr.onload = function () {
    // A hub turning away an unauthenticated caller is exactly what we want.
    if (xhr.status === 401 || xhr.status === 403) {
      settle(true);
      return;
    }
    if (xhr.status >= 200 && xhr.status < 300) {
      try {
        JSON.parse(xhr.responseText);
        settle(true);
      } catch (e) {
        settle(false);   // HTML at that address is a portal, not the hub
      }
      return;
    }
    settle(false);
  };

  xhr.onerror = function () { settle(false); };
  xhr.ontimeout = function () { settle(false); };

  try {
    xhr.open('GET', ep.base + '/devices', true);   // no access_token
    xhr.send();
  } catch (e) {
    settle(false);
  }
}

// Decides whether a Local request may go ahead, falls back to Cloud where that
// is possible, and otherwise stops - without polling.
function ensureLocalSafe(proceed, fail) {
  if (!verifyLocal()) {
    proceed();
    return;
  }

  probeHome(function (home) {
    if (home) {
      proceed();
      return;
    }

    if (cloudAvailable()) {
      // Seamless: the hub is not on this network, so take the public route.
      setMode(1);
      clearBackoff();
      status(STATUS_LOADING, 'Away - using Cloud');
      proceed();
      return;
    }

    // No cloud route. Say why, and stop: the cached verdict means no further
    // request will be attempted until the user asks for a refresh.
    fail(vpnAvailable() ? 'Not home - connect VPN' : 'Not on home network',
         false);
  });
}

// fail(message, reachable) - reachable is false when nothing came back at all,
// so callers can tell "the hub said no" from "there is no network".
function api(path, done, fail) {
  // Local mode has to prove it is on the right network before any token moves.
  if (!currentMode() && parseEndpoint(settings().HubLocalUrl)) {
    ensureLocalSafe(function () { rawApi(path, done, fail); }, fail);
    return;
  }

  rawApi(path, done, fail);
}

function rawApi(path, done, fail) {
  var ep = endpoint();
  if (!ep) {
    // Says which of "not set", "no token" or "refused as unsafe" it is.
    fail(endpointError(), true);
    return;
  }

  if (Date.now() < coolingUntil) {
    fail(unreachableText(), false);
    return;
  }

  var url = ep.base + path;
  url += (url.indexOf('?') >= 0 ? '&' : '?') + 'access_token=' + ep.token;

  var xhr = new XMLHttpRequest();
  xhr.timeout = requestTimeout();

  xhr.onload = function () {
    noteReachable();
    if (xhr.status < 200 || xhr.status >= 300) {
      fail('Hub returned ' + xhr.status, true);
      return;
    }
    var data;
    try {
      data = JSON.parse(xhr.responseText);
    } catch (e) {
      fail('Unreadable hub reply', true);
      return;
    }
    done(data);
  };

  xhr.onerror = function () {
    noteUnreachable();
    fail(unreachableText(), false);
  };

  xhr.ontimeout = function () {
    noteUnreachable();
    fail(currentMode() ? 'Cloud timed out' : 'Hub timed out', false);
  };

  try {
    xhr.open('GET', url, true);
    xhr.send();
  } catch (e) {
    fail('Bad hub URL', true);
  }
}

// ------------------------------------------------------- device modelling ---

function capabilitySet(raw) {
  var out = {};
  var caps = raw.capabilities || [];
  for (var i = 0; i < caps.length; i++)
    if (typeof caps[i] === 'string')
      out[caps[i].toLowerCase()] = true;
  return out;
}

// /devices/all returns attributes as an array of {name, currentValue}, and can
// repeat a name with a null value alongside the real reading.
function attributeMap(raw) {
  var out = {};
  var attrs = raw.attributes;
  if (!attrs)
    return out;

  if (Object.prototype.toString.call(attrs) === '[object Array]') {
    for (var i = 0; i < attrs.length; i++) {
      var a = attrs[i];
      if (!a || !a.name)
        continue;
      if (out[a.name] === undefined || out[a.name] === null ||
          (a.currentValue !== null && a.currentValue !== undefined))
        out[a.name] = a.currentValue;
    }
  } else {
    for (var key in attrs)
      out[key] = attrs[key];
  }

  return out;
}

function onOff(value) {
  if (value === 'on') return '1';
  if (value === 'off') return '0';
  return '?';
}

function openClosed(value) {
  if (!value) return '?';
  var v = String(value).toLowerCase();
  if (v.indexOf('clos') === 0) return 'c';
  if (v.indexOf('open') === 0 || v.indexOf('partial') === 0) return 'o';
  return '?';
}

function level(value) {
  var n = parseInt(value, 10);
  if (isNaN(n)) return -1;
  if (n < 0) n = 0;
  if (n > 100) n = 100;
  return n;
}

function titleCase(value) {
  var v = String(value);
  return v.charAt(0).toUpperCase() + v.slice(1);
}

// The first reading that is present becomes the row's summary. Sensors show
// nothing else, so the order decides what a multi-sensor reports.
var DETAIL_ATTRIBUTES = [
  ['temperature', function (v) { return Math.round(v) + '°'; }],
  ['thermostatOperatingState', titleCase],
  ['motion', titleCase],
  ['contact', titleCase],
  ['presence', titleCase],
  ['water', titleCase],
  ['smoke', titleCase],
  ['carbonMonoxide', titleCase],
  ['valve', titleCase],
  ['humidity', function (v) { return Math.round(v) + '% RH'; }],
  ['illuminance', function (v) { return Math.round(v) + ' lx'; }],
  ['power', function (v) { return Math.round(v) + ' W'; }],
  ['energy', function (v) { return (Math.round(v * 10) / 10) + ' kWh'; }],
  ['battery', function (v) { return Math.round(v) + '%'; }]
];

function detailText(attrs) {
  for (var i = 0; i < DETAIL_ATTRIBUTES.length; i++) {
    var name = DETAIL_ATTRIBUTES[i][0];
    var value = attrs[name];
    if (value === undefined || value === null || value === '')
      continue;
    return clean(DETAIL_ATTRIBUTES[i][1](value)).substring(0, DETAIL_MAX);
  }
  return '';
}

function clean(value) {
  return String(value).replace(/[\x1e\x1f\r\n]/g, ' ').replace(/\s+/g, ' ').trim();
}

// Classifies one Maker API device into the small set of kinds the watch knows
// how to drive. Order matters: a smart lock also reports a battery, and a
// dimmer also reports a plain switch.
function describe(raw) {
  if (!raw || raw.id === undefined || raw.id === null)
    return null;

  var caps = capabilitySet(raw);
  var attrs = attributeMap(raw);

  var device = {
    id: parseInt(raw.id, 10),
    name: clean(raw.label || raw.name || ('Device ' + raw.id)).substring(0, NAME_MAX),
    kind: 'X',
    hasSwitch: !!caps['switch'],
    value: '?',
    level: -1,
    detail: detailText(attrs)
  };

  if (isNaN(device.id))
    return null;

  if (caps['lock']) {
    device.kind = 'L';
    device.value = attrs.lock === 'locked' ? 'l'
                 : (attrs.lock === 'unlocked' ? 'u' : '?');
  } else if (caps['garagedoorcontrol'] || caps['doorcontrol']) {
    device.kind = 'O';
    device.value = openClosed(attrs.door);
  } else if (caps['windowshade']) {
    device.kind = 'W';
    device.value = openClosed(attrs.windowShade);
    device.level = level(attrs.position);
  } else if (caps['audiovolume'] ||
             (caps['musicplayer'] && attrs.volume !== undefined)) {
    device.kind = 'V';
    device.level = level(attrs.volume);
    // Without a switch, select mutes and unmutes instead.
    device.value = device.hasSwitch ? onOff(attrs['switch'])
                 : (attrs.mute === 'muted' ? '0'
                 : (attrs.mute === 'unmuted' ? '1' : '?'));
  } else if (caps['switchlevel']) {
    device.kind = 'D';
    device.level = level(attrs.level);
    device.value = onOff(attrs['switch']);
  } else if (caps['switch']) {
    device.kind = 'S';
    device.value = onOff(attrs['switch']);
  } else if (caps['momentary'] || caps['pushablebutton']) {
    device.kind = 'B';
  }

  return device;
}

var KIND_ORDER = { S: 0, D: 1, V: 2, L: 3, O: 4, W: 5, B: 6, X: 7 };

function sortDevices(list) {
  var mode = sortMode();
  if (mode === 'hub')
    return list;

  list.sort(function (a, b) {
    if (mode === 'kind' && KIND_ORDER[a.kind] !== KIND_ORDER[b.kind])
      return KIND_ORDER[a.kind] - KIND_ORDER[b.kind];
    var an = a.name.toLowerCase();
    var bn = b.name.toLowerCase();
    return an < bn ? -1 : (an > bn ? 1 : 0);
  });

  return list;
}

function buildList(data) {
  var list = [];
  var sensors = showSensors();

  for (var i = 0; i < data.length; i++) {
    var device = describe(data[i]);
    if (!device)
      continue;
    if (!sensors && device.kind === 'X')
      continue;
    list.push(device);
  }

  return sortDevices(list).slice(0, deviceLimit);
}

function reindex(list) {
  deviceList = list;
  deviceIndex = {};
  for (var i = 0; i < list.length; i++)
    deviceIndex[list[i].id] = list[i];
}

// ------------------------------------------------------------- list cache ---

function readCache() {
  try {
    var sig = cacheSignature();
    if (!sig)
      return null;
    var raw = localStorage.getItem(CACHE_KEY);
    if (!raw)
      return null;
    var hit = JSON.parse(raw);
    if (!hit || hit.sig !== sig || !hit.list || !hit.list.length)
      return null;
    // States are deliberately not cached; they are re-read on every launch.
    for (var i = 0; i < hit.list.length; i++) {
      hit.list[i].value = '?';
      hit.list[i].level = -1;
      hit.list[i].detail = '';
    }
    return hit.list.slice(0, deviceLimit);
  } catch (e) {
    return null;
  }
}

function writeCache(list) {
  try {
    var sig = cacheSignature();
    if (!sig)
      return;
    var slim = [];
    for (var i = 0; i < list.length; i++)
      slim.push({
        id: list[i].id,
        name: list[i].name,
        kind: list[i].kind,
        hasSwitch: list[i].hasSwitch
      });
    localStorage.setItem(CACHE_KEY, JSON.stringify({ sig: sig, list: slim }));
  } catch (e) {
    console.log('hubitat: could not cache the device list');
  }
}

function clearCache() {
  try {
    localStorage.removeItem(CACHE_KEY);
  } catch (e) { /* nothing to clear */ }
}

// --------------------------------------------------------------- transfer ---

function listRecord(d) {
  return [d.id, d.name, d.kind, d.value,
          d.level < 0 ? '-' : d.level, d.detail].join(FS) + RS;
}

function stateRecord(d) {
  return [d.id, d.value, d.level < 0 ? '-' : d.level, d.detail].join(FS) + RS;
}

function sendChunks(msgType, list, record) {
  var budget = chunkBudget();
  var i = 0;

  while (i < list.length) {
    var start = i;
    var payload = record(list[i]);
    i++;

    while (i < list.length) {
      var next = record(list[i]);
      if (payload.length + next.length > budget)
        break;
      payload += next;
      i++;
    }

    send({ Msg: msgType, Num: start, Payload: payload });
  }
}

function sendList(list) {
  send({ Msg: MSG_LIST_BEGIN, Num: list.length, Mode: currentMode(),
         Step: stepSize() });
  sendChunks(MSG_LIST_CHUNK, list, listRecord);
  send({ Msg: MSG_LIST_END, Num: list.length });
  lastListAt = Date.now();
}

function sendStates(list) {
  sendChunks(MSG_STATE_CHUNK, list, stateRecord);
}

// ------------------------------------------------------------ operations ---

// Applies fresh readings from /devices/all onto the list already on the watch.
// Devices added to the hub since the list was cached are ignored until the user
// asks for a refresh, which is what keeps the list stable.
function applyStates(data) {
  var touched = [];

  for (var i = 0; i < data.length; i++) {
    var fresh = describe(data[i]);
    if (!fresh)
      continue;
    var known = deviceIndex[fresh.id];
    if (!known)
      continue;

    known.value = fresh.value;
    known.level = fresh.level;
    known.detail = fresh.detail;
    known.hasSwitch = fresh.hasSwitch;
    touched.push(known);
  }

  return touched;
}

function loadStates() {
  // Nothing to update yet - the list has to exist first.
  if (!deviceList.length) {
    loadList(false);
    return;
  }

  api('/devices/all', function (data) {
    sendStates(applyStates(data));
    status(STATUS_IDLE, 'Ready');
  }, function (message) {
    status(STATUS_ERROR, message);
  });
}

function loadList(force) {
  if (listBusy)
    return;

  // The watch asks once at launch and again when the phone's JS reports in;
  // without this the list would be sent twice.
  if (!force && deviceList.length && Date.now() - lastListAt < 3000) {
    loadStates();
    return;
  }

  // An explicit refresh is the user saying "try again now" - typically because
  // they have just walked back into range - so it always gets a real attempt.
  if (force) {
    clearBackoff();
    clearHomeVerdict();   // they may have just walked in the door
    clearCache();
  }

  var cached = force ? null : readCache();
  if (cached) {
    reindex(cached);
    sendList(cached);
    status(STATUS_LOADING, 'Reading states...');
    loadStates();
    return;
  }

  listBusy = true;
  status(STATUS_LOADING, 'Reading hub...');

  api('/devices/all', function (data) {
    listBusy = false;

    if (Object.prototype.toString.call(data) !== '[object Array]') {
      status(STATUS_ERROR, 'Unexpected hub reply');
      return;
    }

    var list = buildList(data);
    if (!list.length) {
      send({ Msg: MSG_LIST_BEGIN, Num: 0 });
      send({ Msg: MSG_LIST_END, Num: 0 });
      status(STATUS_ERROR, 'No devices exposed');
      return;
    }

    reindex(list);
    writeCache(list);
    sendList(list);
    status(STATUS_IDLE, 'Ready');
  }, function (message) {
    listBusy = false;
    status(STATUS_ERROR, message);
  });
}

function refreshOne(id) {
  var known = deviceIndex[id];
  if (!known)
    return;

  api('/devices/' + id, function (data) {
    var fresh = describe(data);
    if (!fresh)
      return;
    known.value = fresh.value;
    known.level = fresh.level;
    known.detail = fresh.detail;
    known.hasSwitch = fresh.hasSwitch;
    sendStates([known]);
  }, function (message) {
    status(STATUS_ERROR, message);
  });
}

// Translates the watch's four intents into whatever this device actually
// accepts, so the watch never has to know a Hubitat command name.
function commandPath(device, cmd, arg) {
  var value = Math.max(0, Math.min(100, parseInt(arg, 10) || 0));

  if (cmd === 'level') {
    if (device.kind === 'D') return 'setLevel/' + value;
    if (device.kind === 'V') return 'setVolume/' + value;
    if (device.kind === 'W') return 'setPosition/' + value;
    return null;
  }

  if (cmd === 'push')
    return 'push';

  var on = cmd === 'on';

  switch (device.kind) {
    case 'L': return on ? 'lock' : 'unlock';
    case 'O':
    case 'W': return on ? 'open' : 'close';
    case 'V': return device.hasSwitch ? (on ? 'on' : 'off')
                                      : (on ? 'unmute' : 'mute');
    case 'B': return 'push';
    case 'S':
    case 'D': return on ? 'on' : 'off';
    default: return null;
  }
}

// Locks, doors and shades move for several seconds after they acknowledge, so
// their confirming read is deliberately late.
function settleDelay(kind) {
  return (kind === 'L' || kind === 'O' || kind === 'W') ? 3000 : 1000;
}

// Tapping a light half a dozen times should cost six commands and one read, not
// six of each, so a device's pending confirmation is replaced rather than added
// to. Nothing is left behind on exit: the JS is torn down with the watch app.
var pendingReads = {};

function scheduleConfirmingRead(id, delay) {
  if (pendingReads[id])
    clearTimeout(pendingReads[id]);

  pendingReads[id] = setTimeout(function () {
    delete pendingReads[id];
    refreshOne(id);
  }, delay);
}

function runCommand(id, cmd, arg) {
  var device = deviceIndex[id];
  if (!device) {
    status(STATUS_ERROR, 'Unknown device');
    return;
  }

  var path = commandPath(device, cmd, arg);
  if (!path) {
    status(STATUS_ERROR, device.name + ' cannot do that');
    return;
  }

  api('/devices/' + id + '/' + path, function () {
    // The command reply is not a reliable snapshot on every hub firmware, so
    // the device is read back once it has had time to settle.
    scheduleConfirmingRead(id, settleDelay(device.kind));
  }, function (message, reachable) {
    status(STATUS_ERROR, message);
    // With no network the confirming read is just another doomed request; the
    // watch's optimistic value gets corrected by the next successful refresh.
    if (reachable)
      scheduleConfirmingRead(id, 500);
  });
}

// ------------------------------------------------------------------ events ---

Pebble.addEventListener('ready', function () {
  send({ Msg: MSG_READY, Num: 0, Mode: currentMode(), Step: stepSize() });
});

Pebble.addEventListener('appmessage', function (e) {
  var p = e.payload || {};
  if (p.Req === undefined)
    return;

  switch (p.Req) {
    case OP_HELLO:
      // The watch reports the inbox it managed to allocate and the number of
      // rows it can hold; both vary by platform.
      if (p.ReqArg > 0)
        watchInbox = p.ReqArg;
      if (p.ReqId > 0)
        deviceLimit = p.ReqId;
      break;

    case OP_LIST:
      loadList(false);
      break;

    case OP_REFRESH:
      loadList(true);
      break;

    case OP_STATES:
      loadStates();
      break;

    case OP_DEVICE:
      refreshOne(p.ReqId);
      break;

    case OP_COMMAND:
      runCommand(p.ReqId, p.ReqCmd, p.ReqArg);
      break;

    case OP_MODE:
      if (p.ReqArg && !cloudAvailable()) {
        // Refuse rather than silently sit in a mode that cannot reach anything.
        setMode(0);
        status(STATUS_ERROR, 'Cloud access is turned off');
        break;
      }
      setMode(p.ReqArg);
      // A different route deserves a fresh attempt: the whole point of flipping
      // to Cloud is that Local was the thing that could not be reached, and
      // going back to Local deserves a fresh look at where we are.
      clearBackoff();
      clearHomeVerdict();
      status(STATUS_LOADING, p.ReqArg ? 'Switched to cloud'
                                      : 'Switched to local');
      break;

    default:
      break;
  }
});

Pebble.addEventListener('showConfiguration', function () {
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', function (e) {
  if (!e || !e.response)
    return;

  // getSettings writes clay-settings itself; nothing is sent to the watch here
  // because the two hub URLs would not fit its inbox.
  clay.getSettings(e.response, false);

  // The settings page is the authority on mode again, until the watch's own
  // Connection row overrides it.
  setMode(settings().HubMode === 'cloud' ? 1 : 0);

  // A changed hub or token invalidates the cached list; a changed sensor or
  // sort preference changes which devices belong on it. New settings also
  // deserve a real attempt rather than an inherited cooldown.
  clearBackoff();
  clearHomeVerdict();
  clearCache();
  deviceList = [];
  deviceIndex = {};
  lastListAt = 0;

  loadList(true);
});
