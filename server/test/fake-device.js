/* Simulates an ERemote device for end-to-end testing without hardware:
 * claims, connects to MQTT, publishes full state, and handles the remote
 * action protocol (send/genset/time/sched_add/sched_del) exactly like the
 * firmware, echoing new state back. Received commands are logged to CMD_LOG.
 *
 * Usage: node fake-device.js [http://host:port] [deviceId]
 */
'use strict';
const fs = require('fs');
const mqtt = require('mqtt');

const BASE = process.argv[2] || 'http://127.0.0.1:8080';
const ID = process.argv[3] || 'dabc123';
const SECRET = 'deadbeef'.repeat(4);
const CMD_LOG = process.env.CMD_LOG || __dirname + '/cmds.log';

const state = {
  online: true,
  codes: { on: true, off: true, eco: false },
  genset: { detected: false, mode: 'disabled', delay: 3, ssid: 'GENSET_ACTIVE' },
  time: { valid: true, epoch: Math.floor(Date.now() / 1000), ntp: true, tz: 'Asia/Baghdad' },
  schedules: [],
  rssi: -55,
};

async function main() {
  const res = await fetch(BASE + '/api/claim', {
    method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ id: ID, secret: SECRET, fw: 'test' }),
  });
  const claim = await res.json();
  if (!claim.ok) throw new Error('claim failed: ' + JSON.stringify(claim));
  console.log('claimed:', JSON.stringify(claim));

  const c = mqtt.connect(`mqtt://${claim.mqtt.host}:${claim.mqtt.port}`, {
    clientId: ID, username: ID, password: SECRET,
    will: { topic: `er/${ID}/state`, payload: '{"online":false}', retain: true },
  });
  const pub = () => c.publish(`er/${ID}/state`, JSON.stringify(state), { retain: true });
  c.on('connect', () => { console.log('mqtt connected'); c.subscribe(`er/${ID}/cmd`); pub(); });
  c.on('message', (topic, payload) => {
    const line = payload.toString();
    console.log('cmd:', line);
    fs.appendFileSync(CMD_LOG, line + '\n');
    let d; try { d = JSON.parse(line); } catch (e) { return; }
    const a = d.a || (d.btn ? 'send' : '');
    if (a === 'genset') {
      state.genset.mode = ['off', 'eco'].includes(d.mode) ? d.mode : 'disabled';
      state.genset.delay = d.delay || 3;
    } else if (a === 'time') {
      state.time.ntp = d.ntp !== false; state.time.tz = d.tz || state.time.tz;
    } else if (a === 'sched_add') {
      state.schedules.push({ id: d.id || Date.now(), action: d.action || 'on',
        hour: d.hour | 0, min: d.min | 0, days: d.days || [] });
    } else if (a === 'sched_del') {
      state.schedules = state.schedules.filter(s => s.id !== d.id);
    }
    pub();
  });
  c.on('error', (e) => console.error('mqtt error:', e.message));
}
main().catch((e) => { console.error(e); process.exit(1); });
