/* ERemote central server.
 *
 * One process runs everything:
 *   - an embedded MQTT broker (aedes) on MQTT_PORT for the devices,
 *     authenticating straight against the device DB (no password files);
 *   - an Express app on PORT: the claim endpoint, and the per-device
 *     customer control pages at /r/<code> with state/cmd/SSE APIs.
 *
 * In production, Caddy sits in front of the Express port for HTTPS.
 * See deploy.md. Protocol contract: ../ERemote/PROTOCOL.md.
 *
 * Env:
 *   PORT             HTTP port                   (default 8080)
 *   MQTT_PORT        MQTT listener port          (default 1883)
 *   BASE_URL         public base for links, e.g. https://er.example.com
 *                    (default http://localhost:<PORT>)
 *   MQTT_PUBLIC_HOST hostname devices should connect to
 *                    (default: hostname of BASE_URL)
 *   DATA_FILE        JSON database path           (default ./data.json)
 */
'use strict';

const express = require('express');
const aedes   = require('aedes')();
const net     = require('net');
const fs      = require('fs');
const path    = require('path');
const crypto  = require('crypto');

const PORT       = +(process.env.PORT || 8080);
const MQTT_PORT  = +(process.env.MQTT_PORT || 1883);
const BASE_URL   = (process.env.BASE_URL || `http://localhost:${PORT}`).replace(/\/+$/, '');
const MQTT_HOST  = process.env.MQTT_PUBLIC_HOST || new URL(BASE_URL).hostname;
const DATA_FILE  = process.env.DATA_FILE || path.join(__dirname, 'data.json');

/* ------------------------------- device DB ------------------------------- */
// { devices: { id: { secret, code, createdAt, domain?, name?, profile?, linkPin? } },
//   codes:   { CODE: id },
//   domains: { name: { pin, createdAt, library: { profile: {on,off,eco} } } } }
// domain/name/profile/linkPin and the whole domains map are OPTIONAL: devices
// claimed before these features simply lack them and behave exactly as before.
let db = { devices: {}, codes: {}, domains: {} };
try { db = JSON.parse(fs.readFileSync(DATA_FILE, 'utf8')); } catch (e) { /* first run */ }
if (!db.devices) db.devices = {};
if (!db.codes) db.codes = {};
if (!db.domains) db.domains = {};

function saveDb() {
  const tmp = DATA_FILE + '.tmp';
  fs.writeFileSync(tmp, JSON.stringify(db, null, 1));
  fs.renameSync(tmp, DATA_FILE);
}

// 6-char code, unambiguous alphabet (no 0/O/1/I/L). See PROTOCOL.md.
const ALPHABET = '23456789ABCDEFGHJKMNPQRSTUVWXYZ';
function genCode() {
  for (;;) {
    let c = '';
    for (let i = 0; i < 6; i++) c += ALPHABET[crypto.randomInt(ALPHABET.length)];
    if (!db.codes[c]) return c;
  }
}

const ID_RE     = /^d[0-9a-f]{1,8}$/;
const SECRET_RE = /^[0-9a-f]{32}$/;
const BTNS      = ['on', 'off', 'eco'];
const DOMAIN_RE = /^[a-z0-9][a-z0-9-]{1,23}$/;   // 2-24 chars, lowercased
const PIN_RE    = /^[0-9]{4}$/;
const PROFILE_RE = /^[A-Za-z0-9][A-Za-z0-9 _-]{0,23}$/;

/* ----------------------------- live state cache --------------------------- */
const states = new Map();      // id -> { state: {...}, lastSeen: ms }
const sseClients = new Map();  // id -> Set(res)

function snapshot(id) {
  const e = states.get(id);
  if (!e) return { online: false, lastSeen: 0 };
  return { ...e.state, lastSeen: e.lastSeen };
}

function pushState(id) {
  const set = sseClients.get(id);
  if (!set) return;
  const data = `data: ${JSON.stringify(snapshot(id))}\n\n`;
  for (const res of set) res.write(data);
}

/* ------------------------------ MQTT broker ------------------------------- */
aedes.authenticate = (client, username, password, cb) => {
  const dev = db.devices[username || ''];
  const ok = !!dev && !!password && password.toString() === dev.secret;
  if (ok) client.deviceId = username;
  cb(null, ok);
};

// Each device may only touch er/<its-own-id>/...
aedes.authorizePublish = (client, packet, cb) => {
  if (!client) return cb(null);                          // internal publishes
  if (packet.topic.startsWith(`er/${client.deviceId}/`)) return cb(null);
  cb(new Error('forbidden'));
};
aedes.authorizeSubscribe = (client, sub, cb) => {
  if (sub.topic.startsWith(`er/${client.deviceId}/`)) return cb(null, sub);
  cb(new Error('forbidden'));
};

// Cache every state publish (device publishes AND broker-delivered wills).
aedes.on('publish', (packet) => {
  const m = /^er\/([^/]+)\/state$/.exec(packet.topic);
  if (!m) return;
  try {
    const state = JSON.parse(packet.payload.toString());
    states.set(m[1], { state, lastSeen: Date.now() });
    pushState(m[1]);
  } catch (e) { /* ignore malformed */ }
});

net.createServer(aedes.handle).listen(MQTT_PORT, () =>
  console.log(`[mqtt] listening on :${MQTT_PORT}`));

/* -------------------------------- HTTP app -------------------------------- */
const app = express();
app.disable('x-powered-by');
app.set('trust proxy', 'loopback');        // Caddy runs on the same box
app.use(express.json({ limit: '16kb' }));   // IR raw arrays travel through here

// Per-IP rate limit for anything that carries an access code: makes guessing
// 6-char codes impractical (31^6 combos at 30 tries/min ≈ forever).
const attempts = new Map();                // ip -> { n, resetAt }
function rateLimit(req, res, next) {
  const now = Date.now();
  let a = attempts.get(req.ip);
  if (!a || now > a.resetAt) { a = { n: 0, resetAt: now + 60000 }; attempts.set(req.ip, a); }
  if (++a.n > 30) return res.status(429).send('Too many requests');
  next();
}
setInterval(() => {                        // don't let the map grow forever
  const now = Date.now();
  for (const [ip, a] of attempts) if (now > a.resetAt) attempts.delete(ip);
}, 300000).unref();

function findByCode(req, res, cb) {
  const code = String(req.params.code || '').toUpperCase();
  const id = db.codes[code];
  if (!id) return setTimeout(() => res.status(404).send('Not found'), 500);
  cb(id);
}

/* ---- device claim ---- */
app.post('/api/claim', (req, res) => {
  const { id, secret } = req.body || {};
  if (!ID_RE.test(id || '') || !SECRET_RE.test(secret || ''))
    return res.status(400).json({ ok: false, error: 'bad-request' });

  let dev = db.devices[id];
  if (dev && dev.secret !== secret) {
    console.log(`[claim] REJECT ${id}: secret mismatch`);
    return res.status(403).json({ ok: false, error: 'secret-mismatch' });
  }
  let dirty = false;
  if (!dev) {
    dev = { secret, code: genCode(), createdAt: Date.now() };
    db.devices[id] = dev;
    db.codes[dev.code] = id;
    dirty = true;
    console.log(`[claim] NEW ${id} -> code ${dev.code}`);
  }

  // Optional domain join. Old firmware omits these fields entirely -> no-op.
  const dom = String((req.body || {}).domain || '').toLowerCase();
  const pin = String((req.body || {}).pin || '');
  if (dom) {
    if (!DOMAIN_RE.test(dom) || !PIN_RE.test(pin))
      return res.status(400).json({ ok: false, error: 'bad-domain' });
    const existing = db.domains[dom];
    if (existing) {
      if (existing.pin !== pin)
        return res.status(403).json({ ok: false, error: 'domain-pin' });
    } else {
      db.domains[dom] = { pin, createdAt: Date.now(), library: {} };
      console.log(`[domain] NEW ${dom}`);
    }
    if (dev.domain !== dom) { dev.domain = dom; console.log(`[domain] ${id} -> ${dom}`); }
    dirty = true;
  }

  // Optional link PIN, gating the personal /r/CODE page against an
  // accidentally-shared code. Firmware that supports it always sends the
  // field (possibly ""); firmware that doesn't omits it entirely -> no-op,
  // so an existing PIN set some other way (there isn't one yet, but this
  // keeps the door open) is never silently cleared by an old device.
  if (Object.prototype.hasOwnProperty.call(req.body || {}, 'linkPin')) {
    const lp = String(req.body.linkPin || '');
    if (lp && !PIN_RE.test(lp))
      return res.status(400).json({ ok: false, error: 'bad-linkpin' });
    if ((dev.linkPin || '') !== lp) {
      dev.linkPin = lp;
      dirty = true;
      console.log(`[linkpin] ${id} ${lp ? 'set' : 'cleared'}`);
    }
  }
  if (dirty) saveDb();
  // Always hand back an https link on the public domain. Prefer the Host the
  // device actually reached us on (er.my.to via Caddy); fall back to BASE_URL.
  // This avoids ever returning a raw http://IP link.
  const host = String(req.headers.host || '').split(':')[0];
  const base = host ? `https://${host}` : BASE_URL.replace(/^http:/, 'https:');
  res.json({
    ok: true,
    code: dev.code,
    link: `${base}/r/${dev.code}`,
    mqtt: { host: MQTT_HOST, port: MQTT_PORT },
  });
});

/* ---- customer control page + APIs ---- */

// Optional link-PIN gate: protects a device's personal page/API if its code
// was ever shared by mistake. The device's identity (and code) survives a
// factory reset by design, so resetting doesn't help there -- a PIN set from
// the portal or wizard is the remedy, and it lives server-side so a browser
// that never entered it can't read state or send commands even knowing the
// code. No PIN set (the default) means the page behaves exactly as before.
function parseCookies(req) {
  const out = {};
  const h = req.headers.cookie;
  if (!h) return out;
  for (const part of h.split(';')) {
    const i = part.indexOf('=');
    if (i < 0) continue;
    out[part.slice(0, i).trim()] = decodeURIComponent(part.slice(i + 1).trim());
  }
  return out;
}
const pinSessions = new Map();   // token -> { id, expires }
const PIN_SESSION_MS = 30 * 24 * 3600000;   // 30 days
setInterval(() => {
  const now = Date.now();
  for (const [t, s] of pinSessions) if (now > s.expires) pinSessions.delete(t);
}, 3600000).unref();

// Failed-unlock lockout, keyed by CODE (not IP) since the code itself is
// what's protected: 8 wrong tries in 10 min locks that code out for 15 min,
// regardless of which IP is trying.
const pinFails = new Map();      // code -> { n, resetAt, lockUntil }
function pinLockedUntil(code) {
  const e = pinFails.get(code);
  return (e && e.lockUntil > Date.now()) ? e.lockUntil : 0;
}
function notePinFail(code) {
  const now = Date.now();
  let e = pinFails.get(code);
  if (!e || now > e.resetAt) e = { n: 0, resetAt: now + 600000, lockUntil: 0 };
  e.n++;
  if (e.n >= 8) e.lockUntil = now + 900000;
  pinFails.set(code, e);
}
setInterval(() => {
  const now = Date.now();
  for (const [c, e] of pinFails) if (now > e.resetAt && now > e.lockUntil) pinFails.delete(c);
}, 600000).unref();

function lookupId(code) { return db.codes[String(code || '').toUpperCase()] || null; }

// Gate for state/cmd/events: 404 if the code doesn't exist, 401 if it exists
// but needs a PIN this browser hasn't unlocked, else attaches req.devId.
function requireUnlocked(req, res, next) {
  const id = lookupId(req.params.code);
  if (!id) return setTimeout(() => res.status(404).send('Not found'), 500);
  const dev = db.devices[id];
  if (!dev.linkPin) { req.devId = id; return next(); }
  const token = parseCookies(req)['erpin_' + dev.code];
  const sess = token && pinSessions.get(token);
  if (sess && sess.id === id && Date.now() < sess.expires) { req.devId = id; return next(); }
  res.status(401).json({ ok: false, error: 'pin-required' });
}

app.get('/r/:code', rateLimit, (req, res) => {
  findByCode(req, res, () =>
    res.sendFile(path.join(__dirname, 'public', 'remote.html')));
});

// Submit the link PIN; on success sets a cookie this browser reuses for 30
// days. A no-op success if the device has no PIN set.
app.post('/api/r/:code/unlock', rateLimit, (req, res) => {
  const id = lookupId(req.params.code);
  if (!id) return setTimeout(() => res.status(404).send('Not found'), 500);
  const dev = db.devices[id];
  if (!dev.linkPin) return res.json({ ok: true });
  const lockUntil = pinLockedUntil(dev.code);
  if (lockUntil) return res.status(429).json({ ok: false, error: 'locked', retryAt: lockUntil });
  const pin = String((req.body || {}).pin || '');
  if (pin !== dev.linkPin) {
    notePinFail(dev.code);
    return setTimeout(() => res.status(403).json({ ok: false, error: 'bad-pin' }), 400);
  }
  pinFails.delete(dev.code);
  const token = crypto.randomBytes(24).toString('base64url');
  pinSessions.set(token, { id, expires: Date.now() + PIN_SESSION_MS });
  const secure = req.secure || req.headers['x-forwarded-proto'] === 'https';
  res.setHeader('Set-Cookie', `erpin_${dev.code}=${token}; Path=/; HttpOnly; ` +
    `SameSite=Lax; Max-Age=${PIN_SESSION_MS / 1000}${secure ? '; Secure' : ''}`);
  res.json({ ok: true });
});

app.get('/api/r/:code/state', rateLimit, requireUnlocked, (req, res) => {
  res.json(snapshot(req.devId));
});

// Relay a command to the device. Accepts either {btn} (simple send) or a
// full action {a:...} mirroring the device API (send/genset/time/sched_add/
// sched_del) so the personal link has portal parity. Wi-Fi is intentionally
// not relayable (see firmware note). The device validates everything.
const ACTIONS = ['send', 'genset', 'time', 'sched_add', 'sched_del', 'led'];
app.post('/api/r/:code/cmd', rateLimit, requireUnlocked, (req, res) => {
  const body = req.body || {};
  let msg = null;
  if (typeof body.a === 'string' && ACTIONS.includes(body.a)) msg = body;
  else if (BTNS.includes(String(body.btn || ''))) msg = { a: 'send', btn: body.btn };
  if (!msg) return res.status(400).json({ ok: false });
  aedes.publish({ topic: `er/${req.devId}/cmd`, payload: JSON.stringify(msg),
                  qos: 0, retain: false }, () => {});
  res.json({ ok: true });
});

app.get('/api/r/:code/events', rateLimit, requireUnlocked, (req, res) => {
  const id = req.devId;
  res.writeHead(200, {
    'Content-Type': 'text/event-stream',
    'Cache-Control': 'no-cache',
    Connection: 'keep-alive',
  });
  res.write(`data: ${JSON.stringify(snapshot(id))}\n\n`);
  if (!sseClients.has(id)) sseClients.set(id, new Set());
  sseClients.get(id).add(res);
  const hb = setInterval(() => res.write(': hb\n\n'), 25000);
  req.on('close', () => { clearInterval(hb); sseClients.get(id).delete(res); });
});

/* =============================== fleet console ============================ */
// Console auth is a fleet master key, so guessing must be slow. Allow a burst
// of 20 attempts per IP, then hard-throttle to a trickle.
const domTries = new Map();               // ip -> { n, resetAt }
function consoleLimit(req, res, next) {
  const now = Date.now();
  let a = domTries.get(req.ip);
  if (!a || now > a.resetAt) { a = { n: 0, resetAt: now + 600000 }; domTries.set(req.ip, a); }
  a.n++;
  if (a.n > 20 && a.n % 5 !== 0) {        // after 20, only 1 in 5 gets through
    return setTimeout(() => res.status(429).json({ ok: false, error: 'rate' }), 1000);
  }
  next();
}
setInterval(() => {
  const now = Date.now();
  for (const [ip, a] of domTries) if (now > a.resetAt) domTries.delete(ip);
}, 600000).unref();

// Validate {domain,pin} in the body; on success call cb(domainName, domainObj).
function authDomain(req, res, cb) {
  const dom = String((req.body || {}).domain || '').toLowerCase();
  const pin = String((req.body || {}).pin || '');
  const d = db.domains[dom];
  if (!d || d.pin !== pin)
    return setTimeout(() => res.status(403).json({ ok: false, error: 'auth' }), 500);
  cb(dom, d);
}
function pubCmd(id, msg) {
  aedes.publish({ topic: `er/${id}/cmd`, payload: JSON.stringify(msg),
                  qos: 0, retain: false }, () => {});
}
// Codes in `list` that belong to this domain -> device ids.
function domainDeviceIds(dom, list) {
  const out = [];
  for (const c of (Array.isArray(list) ? list : [])) {
    const id = db.codes[String(c).toUpperCase()];
    if (id && db.devices[id] && db.devices[id].domain === dom) out.push(id);
  }
  return out;
}
const ONLINE_MS = 90000;                  // state fresher than this = online

app.get('/console', (req, res) =>
  res.sendFile(path.join(__dirname, 'public', 'console.html')));

// List the domain's devices + profiles.
app.post('/api/console/list', consoleLimit, (req, res) => authDomain(req, res, (dom, d) => {
  const now = Date.now();
  const devices = [];
  for (const [id, dev] of Object.entries(db.devices)) {
    if (dev.domain !== dom) continue;
    const e = states.get(id);
    const online = !!e && (now - e.lastSeen) < ONLINE_MS;
    devices.push({
      code: dev.code, name: dev.name || '', profile: dev.profile || '',
      lastAction: (e && e.state && e.state.lastAction) || 'unknown',
      online, lastSeen: e ? e.lastSeen : 0,
    });
  }
  devices.sort((a, b) => (a.name || a.code).localeCompare(b.name || b.code));
  res.json({ ok: true, devices, profiles: Object.keys(d.library || {}).sort() });
}));

// Rename / renumber a device (room label).
app.post('/api/console/name', consoleLimit, (req, res) => authDomain(req, res, (dom) => {
  const ids = domainDeviceIds(dom, [req.body.code]);
  if (!ids.length) return res.status(404).json({ ok: false });
  db.devices[ids[0]].name = String(req.body.name || '').slice(0, 32);
  saveDb();
  res.json({ ok: true });
}));

// Batch command: fan out a=send to the selected devices.
app.post('/api/console/cmd', consoleLimit, (req, res) => authDomain(req, res, (dom) => {
  const btn = String(req.body.btn || '');
  if (!BTNS.includes(btn)) return res.status(400).json({ ok: false });
  const ids = domainDeviceIds(dom, req.body.codes);
  for (const id of ids) pubCmd(id, { a: 'send', btn });
  res.json({ ok: true, sent: ids.length });
}));

// Batch schedule: fan out a=sched_add to the selected devices.
app.post('/api/console/schedule', consoleLimit, (req, res) => authDomain(req, res, (dom) => {
  const action = String(req.body.action || '');
  if (!BTNS.includes(action)) return res.status(400).json({ ok: false });
  const days = (Array.isArray(req.body.days) ? req.body.days : [])
    .map(Number).filter((n) => n >= 0 && n <= 6);
  if (!days.length) return res.status(400).json({ ok: false, error: 'days' });
  const msg = { a: 'sched_add', action, hour: (req.body.hour | 0),
                min: (req.body.min | 0), days };
  const ids = domainDeviceIds(dom, req.body.codes);
  for (const id of ids) pubCmd(id, msg);
  res.json({ ok: true, sent: ids.length });
}));

// Assign a stored profile to devices: they fetch the codes over HTTP.
app.post('/api/console/assign', consoleLimit, (req, res) => authDomain(req, res, (dom, d) => {
  const profile = String(req.body.profile || '');
  if (!(d.library && d.library[profile])) return res.status(404).json({ ok: false });
  const ids = domainDeviceIds(dom, req.body.codes);
  for (const id of ids) { db.devices[id].profile = profile; pubCmd(id, { a: 'profile', name: profile }); }
  saveDb();
  res.json({ ok: true, sent: ids.length });
}));

// Ask one device to (re)record a button into a profile.
app.post('/api/console/record', consoleLimit, (req, res) => authDomain(req, res, (dom, d) => {
  const profile = String(req.body.profile || '');
  const btn = String(req.body.btn || '');
  if (!PROFILE_RE.test(profile) || !BTNS.includes(btn)) return res.status(400).json({ ok: false });
  const ids = domainDeviceIds(dom, [req.body.code]);
  if (!ids.length) return res.status(404).json({ ok: false });
  if (!d.library[profile]) { d.library[profile] = {}; saveDb(); }   // reserve the name
  pubCmd(ids[0], { a: 'record', profile, btn });
  res.json({ ok: true });
}));

// Delete a stored profile from the domain library.
app.post('/api/console/profile_del', consoleLimit, (req, res) => authDomain(req, res, (dom, d) => {
  const profile = String(req.body.profile || '');
  if (d.library && d.library[profile]) { delete d.library[profile]; saveDb(); }
  res.json({ ok: true });
}));

/* ------- device-facing IR library (auth by id+secret, like /api/claim) ---- */
function authDevice(req) {
  const id = String((req.body && req.body.id) || req.query.id || '');
  const secret = String((req.body && req.body.secret) || req.query.secret || '');
  const dev = db.devices[id];
  if (!dev || dev.secret !== secret || !dev.domain) return null;
  return { id, dev };
}

// A device uploads one recorded button into its domain's profile library.
app.post('/api/ir/upload', (req, res) => {
  const a = authDevice(req);
  if (!a) return res.status(403).json({ ok: false });
  const profile = String(req.body.profile || '');
  const btn = String(req.body.btn || '');
  const raw = req.body.raw;
  if (!PROFILE_RE.test(profile) || !BTNS.includes(btn) ||
      !Array.isArray(raw) || raw.length < 4 || raw.length > 1024)
    return res.status(400).json({ ok: false, error: 'bad-ir' });
  const lib = db.domains[a.dev.domain].library;
  if (!lib[profile]) lib[profile] = {};
  lib[profile][btn] = { raw: raw.map((n) => n | 0), freq: (req.body.freq | 0) || 38 };
  saveDb();
  console.log(`[ir] ${a.id} uploaded ${profile}/${btn} (${raw.length})`);
  res.json({ ok: true });
});

// A device fetches its assigned profile's codes to adopt them.
app.get('/api/ir/profile', (req, res) => {
  const a = authDevice(req);
  if (!a) return res.status(403).json({ ok: false });
  const profile = String(req.query.profile || '');
  const lib = db.domains[a.dev.domain].library;
  if (!lib[profile]) return res.status(404).json({ ok: false });
  res.json({ ok: true, profile, codes: lib[profile] });
});

app.get('/healthz', (req, res) => res.send('ok'));
app.get('/', (req, res) =>
  res.sendFile(path.join(__dirname, 'public', 'index.html')));

app.listen(PORT, () => console.log(`[http] listening on :${PORT}  base ${BASE_URL}`));
