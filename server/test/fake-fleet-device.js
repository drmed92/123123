/* Simulated ERemote for fleet testing: claims into a domain, publishes state
 * incl. lastAction, and handles the console action set (send/sched_add/record/
 * profile). On a=record it fakes a capture and uploads a raw code; on a=profile
 * it fetches and "adopts". Usage:
 *   node fake-fleet-device.js <base> <id> <domain> <pin>
 */
'use strict';
const mqtt = require('mqtt');

const BASE = process.argv[2] || 'http://127.0.0.1:8080';
const ID   = process.argv[3] || 'dfeed01';
const DOM  = process.argv[4] || 'bldg1';
const PIN  = process.argv[5] || '1234';
const SECRET = (ID.replace(/^d/, '') + 'abcdef').padEnd(32, '0').slice(0, 32);

const state = {
  online: true, codes: { on: true, off: true, eco: false },
  lastAction: 'unknown', ledOn: true,
};
let profile = '';

async function main() {
  const res = await fetch(BASE + '/api/claim', {
    method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ id: ID, secret: SECRET, domain: DOM, pin: PIN }),
  });
  const claim = await res.json();
  if (!claim.ok) throw new Error(ID + ' claim failed: ' + JSON.stringify(claim));
  console.log(`${ID} claimed code=${claim.code} domain=${DOM}`);

  const c = mqtt.connect(`mqtt://${claim.mqtt.host}:${claim.mqtt.port}`, {
    clientId: ID, username: ID, password: SECRET,
    will: { topic: `er/${ID}/state`, payload: '{"online":false}', retain: true },
  });
  const pub = () => c.publish(`er/${ID}/state`, JSON.stringify(state), { retain: true });
  c.on('connect', () => { c.subscribe(`er/${ID}/cmd`); pub(); setInterval(pub, 20000); });
  c.on('message', async (topic, payload) => {
    let d; try { d = JSON.parse(payload.toString()); } catch (e) { return; }
    const a = d.a || (d.btn ? 'send' : '');
    if (a === 'send') { state.lastAction = d.btn; pub(); console.log(`${ID} sent ${d.btn}`); }
    else if (a === 'sched_add') console.log(`${ID} scheduled ${d.action} ${d.hour}:${d.min} days=${d.days}`);
    else if (a === 'record') {
      // fake an immediate capture + upload
      const raw = Array.from({ length: 67 }, (_, i) => 400 + (i % 5) * 100);
      const up = await fetch(BASE + '/api/ir/upload', {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ id: ID, secret: SECRET, profile: d.profile, btn: d.btn, raw, freq: 38 }),
      });
      console.log(`${ID} recorded+uploaded ${d.profile}/${d.btn} -> ${(await up.json()).ok}`);
    } else if (a === 'profile') {
      const r = await fetch(`${BASE}/api/ir/profile?id=${ID}&secret=${SECRET}&profile=${encodeURIComponent(d.name)}`);
      const j = await r.json();
      profile = d.name; state.codes = { on: !!j.codes?.on, off: !!j.codes?.off, eco: !!j.codes?.eco };
      pub();
      console.log(`${ID} adopted profile ${d.name} buttons=${Object.keys(j.codes || {})}`);
    }
  });
  c.on('error', (e) => console.error(ID, 'mqtt', e.message));
}
main().catch((e) => { console.error(e); process.exit(1); });
