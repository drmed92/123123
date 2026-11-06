/* ERemote server protocol tests.   Run:  npm test      (Node >= 18, no extra deps)
 *
 * Boots the REAL server (app.js) on free ports with a throw-away DATA_FILE and
 * talks to it the way production clients do:
 *   - as a device:   HTTP claim + authenticated MQTT (id/secret)
 *   - as a customer: /r/<code> web APIs (state, cmd, SSE)
 *   - as an operator: fleet console (domain + PIN)
 * The contract being protected is ../../ERemote/PROTOCOL.md. If you change the
 * wire format on either side, change this file in the same commit.
 *
 * Each test uses its own fake client IP (X-Forwarded-For; the server trusts
 * loopback proxies, exactly like behind Caddy) so the per-IP rate limiter never
 * couples tests together.
 */
'use strict';
const { test, before, after } = require('node:test');
const assert = require('node:assert/strict');
const { spawn } = require('node:child_process');
const crypto = require('node:crypto');
const net = require('node:net');
const os = require('node:os');
const fs = require('node:fs');
const path = require('node:path');
const mqtt = require('mqtt');

const ALPHABET = '23456789ABCDEFGHJKMNPQRSTUVWXYZ';       // PROTOCOL.md: no 0/O/1/I/L
const CODE_RE = new RegExp(`^[${ALPHABET}]{6}$`);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

/* ------------------------------- harness --------------------------------- */
function freePort() {
  return new Promise((resolve, reject) => {
    const s = net.createServer();
    s.once('error', reject);
    s.listen(0, '127.0.0.1', () => { const { port } = s.address(); s.close(() => resolve(port)); });
  });
}

async function startServer(dataFile) {
  const port = await freePort();
  const mqttPort = await freePort();
  const child = spawn(process.execPath, [path.join(__dirname, '..', 'app.js')], {
    env: { ...process.env, PORT: String(port), MQTT_PORT: String(mqttPort),
           BASE_URL: `http://127.0.0.1:${port}`, DATA_FILE: dataFile },
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  let log = '';
  child.stdout.on('data', (d) => { log += d; });
  child.stderr.on('data', (d) => { log += d; });
  const t0 = Date.now();
  for (;;) {
    if (child.exitCode !== null) throw new Error('server exited early:\n' + log);
    try { if ((await fetch(`http://127.0.0.1:${port}/healthz`)).ok && /\[mqtt\] listening/.test(log)) break; } catch (e) { /* not up yet */ }
    if (Date.now() - t0 > 8000) { child.kill(); throw new Error('server did not start:\n' + log); }
    await sleep(50);
  }
  return {
    port, mqttPort, base: `http://127.0.0.1:${port}`,
    stop: () => new Promise((resolve) => {
      if (child.exitCode !== null) return resolve();
      child.once('exit', resolve); child.kill('SIGTERM');
    }),
  };
}

let srv, tmpDir, ipCounter = 0;
const clients = [];                          // every MQTT client, closed in after()

before(async () => {
  tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'eremote-test-'));
  srv = await startServer(path.join(tmpDir, 'data.json'));
});
after(async () => {
  for (const c of clients) c.end(true);
  await srv.stop();
  fs.rmSync(tmpDir, { recursive: true, force: true });
});

/** Fresh fake client IP per call site, so tests don't share a rate-limit bucket. */
const nextIp = () => `198.51.100.${++ipCounter}`;

async function http(method, url, body, ip = nextIp()) {
  const res = await fetch(srv.base + url, {
    method,
    headers: { 'content-type': 'application/json', 'x-forwarded-for': ip },
    body: body === undefined ? undefined : JSON.stringify(body),
  });
  const text = await res.text();
  let json = null; try { json = JSON.parse(text); } catch (e) { /* not JSON */ }
  return { status: res.status, json, text, headers: res.headers };
}

const hex = (bytes) => crypto.randomBytes(bytes).toString('hex');

/** Claim a brand-new device (what the firmware does on first boot). */
async function newDevice(extra = {}) {
  const dev = { id: 'd' + hex(3), secret: hex(16) };
  const r = await http('POST', '/api/claim', { id: dev.id, secret: dev.secret, fw: 'test', ...extra });
  assert.equal(r.status, 200, r.text);
  return { ...dev, code: r.json.code, claim: r.json };
}

function connect(id, secret, { will } = {}) {
  return new Promise((resolve, reject) => {
    const c = mqtt.connect(`mqtt://127.0.0.1:${srv.mqttPort}`, {
      clientId: `${id}-${clients.length}`, username: id, password: secret,
      reconnectPeriod: 0, connectTimeout: 3000, will,
    });
    clients.push(c);
    c.once('connect', () => resolve(c));
    c.once('error', reject);
  });
}
const subscribe = (c, topic) => new Promise((res, rej) =>
  c.subscribe(topic, (err, granted) => (err ? rej(err) : res(granted))));

function inbox(c) {
  const msgs = [];
  c.on('message', (topic, payload) => {
    let json = null; try { json = JSON.parse(payload.toString()); } catch (e) { /* raw */ }
    msgs.push({ topic, json, raw: payload.toString() });
  });
  return msgs;
}

async function waitFor(fn, ms = 3000) {
  const t0 = Date.now();
  for (;;) {
    const v = await fn();
    if (v) return v;
    if (Date.now() - t0 > ms) throw new Error('timed out waiting for: ' + fn);
    await sleep(25);
  }
}

/* --------------------------------- claim ---------------------------------- */
test('healthz answers ok', async () => {
  const r = await http('GET', '/healthz');
  assert.equal(r.status, 200);
  assert.equal(r.text, 'ok');
});

test('claim rejects malformed ids and secrets', async () => {
  const good = { id: 'd1a2b3c', secret: hex(16) };
  for (const bad of [
    {}, { id: good.id }, { secret: good.secret },
    { id: 'x1a2b3c', secret: good.secret },            // must start with "d"
    { id: 'dZZZ', secret: good.secret },               // non-hex
    { id: 'd123456789', secret: good.secret },         // > 8 hex digits
    { id: good.id, secret: good.secret.slice(1) },     // 31 chars
    { id: good.id, secret: good.secret.toUpperCase() } // upper-case hex not allowed
  ]) {
    const r = await http('POST', '/api/claim', bad);
    assert.equal(r.status, 400, JSON.stringify(bad));
    assert.equal(r.json.ok, false);
  }
});

test('claim registers a device, returns a link + broker address, and is idempotent', async () => {
  const dev = await newDevice();
  assert.match(dev.code, CODE_RE);
  assert.equal(dev.claim.ok, true);
  assert.ok(dev.claim.link.endsWith(`/r/${dev.code}`), dev.claim.link);
  assert.deepEqual(dev.claim.mqtt, { host: '127.0.0.1', port: srv.mqttPort });

  const again = await http('POST', '/api/claim', { id: dev.id, secret: dev.secret });
  assert.equal(again.status, 200);
  assert.equal(again.json.code, dev.code, 'same device must always get the same code');
});

test('first claim wins: a different secret for a known id is refused', async () => {
  const dev = await newDevice();
  const r = await http('POST', '/api/claim', { id: dev.id, secret: hex(16) });
  assert.equal(r.status, 403);
  assert.equal(r.json.error, 'secret-mismatch');
  const still = await http('POST', '/api/claim', { id: dev.id, secret: dev.secret });
  assert.equal(still.json.code, dev.code, 'rightful device keeps its registration');
});

/* ------------------------------ customer pages ----------------------------- */
test('unknown access codes are 404 on every customer route', async () => {
  const ip = nextIp();
  for (const [m, u] of [['GET', '/r/ZZZZZZ'], ['GET', '/api/r/ZZZZZZ/state'], ['POST', '/api/r/ZZZZZZ/cmd']]) {
    const r = await http(m, u, m === 'POST' ? { btn: 'on' } : undefined, ip);
    assert.equal(r.status, 404, `${m} ${u}`);
  }
});

test('a valid code serves the control page; codes are case-insensitive', async () => {
  const dev = await newDevice();
  const ip = nextIp();
  const page = await http('GET', `/r/${dev.code}`, undefined, ip);
  assert.equal(page.status, 200);
  assert.match(page.headers.get('content-type'), /html/);
  const lower = await http('GET', `/api/r/${dev.code.toLowerCase()}/state`, undefined, ip);
  assert.equal(lower.status, 200);
});

test('state is offline until the device reports, then mirrors what it published', async () => {
  const dev = await newDevice();
  const ip = nextIp();
  const before = await http('GET', `/api/r/${dev.code}/state`, undefined, ip);
  assert.deepEqual(before.json, { online: false, lastSeen: 0 });

  const c = await connect(dev.id, dev.secret);
  const state = { online: true, codes: { on: true, off: false, eco: false }, genset: false, rssi: -61 };
  c.publish(`er/${dev.id}/state`, JSON.stringify(state), { retain: true });

  const seen = await waitFor(async () => {
    const r = await http('GET', `/api/r/${dev.code}/state`, undefined, ip);
    return r.json.online ? r.json : null;
  });
  assert.deepEqual({ ...seen, lastSeen: undefined }, { ...state, lastSeen: undefined });
  assert.ok(seen.lastSeen > Date.now() - 10000, 'lastSeen is a recent epoch-ms timestamp');
});

test('a dropped connection flips the device offline via its last-will', async () => {
  const dev = await newDevice();
  const ip = nextIp();
  const c = await connect(dev.id, dev.secret, {
    will: { topic: `er/${dev.id}/state`, payload: '{"online":false}', retain: true },
  });
  c.publish(`er/${dev.id}/state`, JSON.stringify({ online: true }), { retain: true });
  await waitFor(async () => (await http('GET', `/api/r/${dev.code}/state`, undefined, ip)).json.online);
  c.stream.destroy();                                   // power loss: no DISCONNECT packet
  await waitFor(async () => (await http('GET', `/api/r/${dev.code}/state`, undefined, ip)).json.online === false);
});

/* ---------------------------------- MQTT ---------------------------------- */
test('broker only accepts a device with its own id + secret', async () => {
  const dev = await newDevice();
  await assert.rejects(connect(dev.id, hex(16)), 'wrong secret');
  await assert.rejects(connect('dffffff0', dev.secret), 'unknown id');
  await assert.rejects(connect(dev.id, ''), 'empty password');
  const ok = await connect(dev.id, dev.secret);
  assert.ok(ok.connected);
});

test('commands from the web page reach the device as {a:"send",btn}', async () => {
  // NOTE: PROTOCOL.md still shows the older bare {"btn":"on"}; the server wraps it
  // in the action envelope the firmware's remote-action handler expects.
  const dev = await newDevice();
  const ip = nextIp();
  const c = await connect(dev.id, dev.secret);
  const got = inbox(c);
  await subscribe(c, `er/${dev.id}/cmd`);

  for (const btn of ['on', 'off', 'eco']) {
    const r = await http('POST', `/api/r/${dev.code}/cmd`, { btn }, ip);
    assert.deepEqual(r.json, { ok: true });
  }
  await waitFor(() => got.length === 3);
  assert.deepEqual(got.map((m) => m.json), [
    { a: 'send', btn: 'on' }, { a: 'send', btn: 'off' }, { a: 'send', btn: 'eco' },
  ]);
  assert.ok(got.every((m) => m.topic === `er/${dev.id}/cmd`));
});

test('full actions are relayed, anything else (wifi, junk) is refused', async () => {
  const dev = await newDevice();
  const ip = nextIp();
  const c = await connect(dev.id, dev.secret);
  const got = inbox(c);
  await subscribe(c, `er/${dev.id}/cmd`);

  const genset = { a: 'genset', mode: 'off', delay: 5 };
  assert.equal((await http('POST', `/api/r/${dev.code}/cmd`, genset, ip)).status, 200);
  await waitFor(() => got.length === 1);
  assert.deepEqual(got[0].json, genset);

  for (const bad of [{ btn: 'explode' }, { a: 'wifi', ssid: 'x', pass: 'y' }, { a: 7 }, {}]) {
    const r = await http('POST', `/api/r/${dev.code}/cmd`, bad, ip);
    assert.equal(r.status, 400, JSON.stringify(bad));
  }
  await sleep(150);
  assert.equal(got.length, 1, 'refused commands must never reach the device');
});

// KNOWN ISSUE (reported, not fixed here): app.js validates String(body.btn) but forwards
// the raw value, so {btn:["on"]} passes the check and the device receives an array.
// The firmware validates again, so impact is low; fix is `btn: String(body.btn)`.
// Remove `todo` once the server is changed and this passes.
test('btn must be a plain string, not something that merely stringifies to one',
  { todo: 'server coerces {btn:["on"]} and forwards the array' }, async () => {
    const dev = await newDevice();
    const r = await http('POST', `/api/r/${dev.code}/cmd`, { btn: ['on'] });
    assert.equal(r.status, 400);
  });

test('devices are sandboxed to their own er/<id>/ topics', async () => {
  const a = await newDevice();
  const b = await newDevice();
  const ip = nextIp();

  const ca = await connect(a.id, a.secret);
  const inA = inbox(ca);
  await subscribe(ca, `er/${a.id}/cmd`);

  // B tries to eavesdrop on A's commands...
  const spy = await connect(b.id, b.secret);
  const inSpy = inbox(spy);
  await subscribe(spy, `er/${a.id}/cmd`).catch(() => { /* refused or dropped: both fine */ });
  // ...and, from a second connection, to forge A's state.
  const forger = await connect(b.id, b.secret);
  try { forger.publish(`er/${a.id}/state`, JSON.stringify({ online: true, spoof: 1 })); } catch (e) { /* dropped */ }

  await http('POST', `/api/r/${a.code}/cmd`, { btn: 'on' }, ip);
  await waitFor(() => inA.length === 1);
  await sleep(300);
  assert.equal(inSpy.length, 0, 'B must not see A\'s commands');
  const st = await http('GET', `/api/r/${a.code}/state`, undefined, ip);
  assert.equal(st.json.online, false);
  assert.equal(st.json.spoof, undefined, 'B must not be able to write A\'s state');
});

test('SSE stream sends the current snapshot, then live updates', async () => {
  const dev = await newDevice();
  const ip = nextIp();
  const c = await connect(dev.id, dev.secret);

  const ac = new AbortController();
  const res = await fetch(`${srv.base}/api/r/${dev.code}/events`, {
    headers: { 'x-forwarded-for': ip }, signal: ac.signal,
  });
  assert.equal(res.status, 200);
  assert.match(res.headers.get('content-type'), /text\/event-stream/);

  const events = [];
  const reader = res.body.getReader();
  const dec = new TextDecoder();
  let buf = '';
  const pump = (async () => {
    for (;;) {
      const { value, done } = await reader.read().catch(() => ({ done: true }));
      if (done) return;
      buf += dec.decode(value, { stream: true });
      let i;
      while ((i = buf.indexOf('\n\n')) >= 0) {
        const line = buf.slice(0, i).split('\n').find((l) => l.startsWith('data: '));
        buf = buf.slice(i + 2);
        if (line) events.push(JSON.parse(line.slice(6)));
      }
    }
  })();

  await waitFor(() => events.length >= 1);
  assert.equal(events[0].online, false, 'first event is the current snapshot');
  c.publish(`er/${dev.id}/state`, JSON.stringify({ online: true, rssi: -42 }));
  await waitFor(() => events.some((e) => e.online === true && e.rssi === -42));
  ac.abort();
  await pump;
});

/* ------------------------------ rate limiting ------------------------------ */
test('customer routes are rate-limited per IP (code-guessing protection)', async () => {
  const ip = nextIp();
  const codes = await Promise.all(Array.from({ length: 40 }, () =>
    http('GET', '/api/r/ZZZZZZ/state', undefined, ip).then((r) => r.status)));
  assert.ok(codes.includes(429), 'expected 429s after 30 attempts/min from one IP');
  assert.ok(codes.filter((s) => s === 404).length <= 30);
  // a different client IP is unaffected
  assert.equal((await http('GET', '/api/r/ZZZZZZ/state')).status, 404);
});

/* ------------------------------- fleet console ----------------------------- */
test('fleet console: PIN-gated domain, batch commands only reach that domain', async () => {
  const dom = 'lab' + hex(3);
  const pin = '4821';
  const a = await newDevice({ domain: dom, pin });
  const b = await newDevice({ domain: dom, pin });
  const outsider = await newDevice();                                   // not in the domain

  // second domain with a different PIN can't be joined with the wrong PIN
  const wrong = await http('POST', '/api/claim', { id: 'd' + hex(3), secret: hex(16), domain: dom, pin: '0000' });
  assert.equal(wrong.status, 403);
  assert.equal(wrong.json.error, 'domain-pin');

  // console auth
  const denied = await http('POST', '/api/console/list', { domain: dom, pin: '0000' });
  assert.equal(denied.status, 403);
  const list = await http('POST', '/api/console/list', { domain: dom, pin });
  assert.equal(list.status, 200);
  assert.deepEqual(list.json.devices.map((d) => d.code).sort(), [a.code, b.code].sort());

  const ca = await connect(a.id, a.secret);
  const cb = await connect(b.id, b.secret);
  const co = await connect(outsider.id, outsider.secret);
  const [inA, inB, inO] = [inbox(ca), inbox(cb), inbox(co)];
  await Promise.all([subscribe(ca, `er/${a.id}/cmd`), subscribe(cb, `er/${b.id}/cmd`), subscribe(co, `er/${outsider.id}/cmd`)]);

  const r = await http('POST', '/api/console/cmd', { domain: dom, pin, btn: 'off', codes: [a.code, b.code, outsider.code] });
  assert.deepEqual(r.json, { ok: true, sent: 2 }, 'outsider code must be ignored');
  await waitFor(() => inA.length === 1 && inB.length === 1);
  assert.deepEqual(inA[0].json, { a: 'send', btn: 'off' });
  await sleep(200);
  assert.equal(inO.length, 0);

  assert.equal((await http('POST', '/api/console/cmd', { domain: dom, pin, btn: 'nope', codes: [a.code] })).status, 400);
});

/* ------------------------------- persistence ------------------------------- */
test('registrations survive a server restart (same code, same secret)', async () => {
  const dataFile = path.join(tmpDir, 'restart.json');
  const dev = { id: 'd' + hex(3), secret: hex(16) };

  const s1 = await startServer(dataFile);
  const first = await (await fetch(`${s1.base}/api/claim`, {
    method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify(dev) })).json();
  await s1.stop();

  const s2 = await startServer(dataFile);
  try {
    const claim = (body) => fetch(`${s2.base}/api/claim`, {
      method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify(body) });
    const again = await (await claim(dev)).json();
    assert.equal(again.code, first.code, 'code must be stable across restarts');
    assert.equal((await claim({ ...dev, secret: hex(16) })).status, 403, 'secret is still enforced');
  } finally {
    await s2.stop();
  }
});
