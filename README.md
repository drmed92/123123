# ERemote

Smart scheduling IR remote for split air-conditioners, built on an ESP8266
(WeMos/LOLIN D1 mini), plus the small central server that gives every device a
personal control link.

| Path | What it is |
|---|---|
| `ERemote/ERemote.ino` | Device firmware (with `portal_html.h` control portal and `wizard_html.h` first-run setup wizard) |
| `ERemote/PROTOCOL.md` | Device ⇄ server contract: claim, MQTT topics, web APIs |
| `server/` | Node.js server: embedded MQTT broker + claim API + customer pages (`/r/<code>`) + fleet console (`/console`) |
| `server/deploy.md` | VPS deployment guide (Caddy + HTTPS, or IP-only) |
| `server/test/` | Automated protocol tests, plus two fake-device scripts for manual end-to-end runs |

## Firmware

Board `esp8266:esp8266:d1_mini` (Arduino IDE or arduino-cli). Libraries:
ArduinoJson v7, IRremoteESP8266, PubSubClient. Wiring and behaviour are described
at the top of `ERemote.ino`. Set `ER_HOST` near the top of the file to your own
server before flashing.

## Server

```bash
cd server
npm ci
npm start          # HTTP on :8080, MQTT on :1883
```

Configuration is by environment variables (`PORT`, `MQTT_PORT`, `BASE_URL`,
`MQTT_PUBLIC_HOST`, `DATA_FILE`), documented at the top of `server/app.js`.
For production see `server/deploy.md`.

## Tests

```bash
cd server
npm ci
npm test
```

The tests start the real server on free ports and exercise `ERemote/PROTOCOL.md`
end to end: device claim, MQTT authentication and topic isolation, web-to-device
commands, state and SSE updates, rate limiting, fleet console and persistence
across restarts. GitHub Actions runs them on every pull request that touches
`server/` or `ERemote/PROTOCOL.md`.
