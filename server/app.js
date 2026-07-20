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
// { devices: { id: { secret, code, createdAt } }, codes: { CODE: id } }
let db = { devices: {}, codes: {} };
try { db = JSON.parse(fs.readFileSync(DATA_FILE, 'utf8')); } catch (e) { /* first run */ }
if (!db.devices) db.devices = {};
if (!db.codes) db.codes = {};

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
app.use(express.json({ limit: '4kb' }));

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
  if (!dev) {
    dev = { secret, code: genCode(), createdAt: Date.now() };
    db.devices[id] = dev;
    db.codes[dev.code] = id;
    saveDb();
    console.log(`[claim] NEW ${id} -> code ${dev.code}`);
  }
  res.json({
    ok: true,
    code: dev.code,
    link: `${BASE_URL}/r/${dev.code}`,
    mqtt: { host: MQTT_HOST, port: MQTT_PORT },
  });
});

/* ---- customer control page + APIs ---- */
app.get('/r/:code', rateLimit, (req, res) => {
  findByCode(req, res, () =>
    res.sendFile(path.join(__dirname, 'public', 'remote.html')));
});

app.get('/api/r/:code/state', rateLimit, (req, res) => {
  findByCode(req, res, (id) => res.json(snapshot(id)));
});

// Relay a command to the device. Accepts either {btn} (simple send) or a
// full action {a:...} mirroring the device API (send/genset/time/sched_add/
// sched_del) so the personal link has portal parity. Wi-Fi is intentionally
// not relayable (see firmware note). The device validates everything.
const ACTIONS = ['send', 'genset', 'time', 'sched_add', 'sched_del'];
app.post('/api/r/:code/cmd', rateLimit, (req, res) => {
  findByCode(req, res, (id) => {
    const body = req.body || {};
    let msg = null;
    if (typeof body.a === 'string' && ACTIONS.includes(body.a)) msg = body;
    else if (BTNS.includes(String(body.btn || ''))) msg = { a: 'send', btn: body.btn };
    if (!msg) return res.status(400).json({ ok: false });
    aedes.publish({ topic: `er/${id}/cmd`, payload: JSON.stringify(msg),
                    qos: 0, retain: false }, () => {});
    res.json({ ok: true });
  });
});

app.get('/api/r/:code/events', rateLimit, (req, res) => {
  findByCode(req, res, (id) => {
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
});

app.get('/healthz', (req, res) => res.send('ok'));
app.get('/', (req, res) =>
  res.sendFile(path.join(__dirname, 'public', 'index.html')));

app.listen(PORT, () => console.log(`[http] listening on :${PORT}  base ${BASE_URL}`));
