/* Simulates an ERemote device for end-to-end testing without hardware:
 * claims against the server, connects to MQTT with the claimed credentials,
 * publishes retained state like the firmware does, and appends every
 * received command to CMD_LOG (default ./cmds.log).
 *
 * Usage: node fake-device.js [http://host:port] [deviceId]
 */
'use strict';
const fs = require('fs');
const mqtt = require('mqtt');

const BASE = process.argv[2] || 'http://127.0.0.1:8080';
const ID = process.argv[3] || 'dabc123';
const SECRET = 'deadbeef'.repeat(4);              // 32 hex, stable for tests
const CMD_LOG = process.env.CMD_LOG || __dirname + '/cmds.log';

async function main() {
  const res = await fetch(BASE + '/api/claim', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ id: ID, secret: SECRET, fw: 'test' }),
  });
  const claim = await res.json();
  if (!claim.ok) throw new Error('claim failed: ' + JSON.stringify(claim));
  console.log('claimed:', JSON.stringify(claim));

  const c = mqtt.connect(`mqtt://${claim.mqtt.host}:${claim.mqtt.port}`, {
    clientId: ID, username: ID, password: SECRET,
    will: { topic: `er/${ID}/state`, payload: '{"online":false}', retain: true },
  });
  const state = { online: true, codes: { on: true, off: true, eco: false },
                  genset: false, rssi: -55 };
  c.on('connect', () => {
    console.log('mqtt connected');
    c.subscribe(`er/${ID}/cmd`);
    c.publish(`er/${ID}/state`, JSON.stringify(state), { retain: true });
  });
  c.on('message', (topic, payload) => {
    const line = payload.toString();
    console.log('cmd received:', line);
    fs.appendFileSync(CMD_LOG, line + '\n');
    state.genset = !state.genset;             // visible state change for SSE test
    c.publish(`er/${ID}/state`, JSON.stringify(state), { retain: true });
  });
  c.on('error', (e) => console.error('mqtt error:', e.message));
}

main().catch((e) => { console.error(e); process.exit(1); });
