# Deploying the ERemote server (Oracle free VM, Ubuntu 22.04 ARM)

One Node process runs everything: the MQTT broker (embedded, port 1883),
the claim API, and the customer control pages. Caddy in front provides
automatic HTTPS when you have a domain. Works identically on x86 VPSes
(Vultr etc.).

## 0. Open the ports (the #1 Oracle gotcha)

Oracle blocks almost everything by default in TWO places:

1. **VCN security list** (web console): Networking → your VCN → Security
   Lists → add ingress rules for TCP **80, 443, 1883** from 0.0.0.0/0.
2. **iptables on the VM itself** (Oracle's Ubuntu images ship reject rules):

```bash
sudo iptables -I INPUT -p tcp --dport 80   -j ACCEPT
sudo iptables -I INPUT -p tcp --dport 443  -j ACCEPT
sudo iptables -I INPUT -p tcp --dport 1883 -j ACCEPT
sudo apt-get install -y iptables-persistent && sudo netfilter-persistent save
```

## 1. Install Node 20 and the app

```bash
curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
sudo apt-get install -y nodejs git
sudo git clone https://github.com/drmed92/123123.git /opt/eremote
cd /opt/eremote/server && sudo npm install --omit=dev
```

## 2. Configure and run as a service

`sudo nano /etc/systemd/system/eremote.service`:

```ini
[Unit]
Description=ERemote server
After=network-online.target
Wants=network-online.target

[Service]
WorkingDirectory=/opt/eremote/server
ExecStart=/usr/bin/node app.js
Restart=always
RestartSec=5
# With a domain (recommended):
Environment=BASE_URL=https://YOUR.DOMAIN
# Without a domain (IP-only mode) use instead:
#Environment=BASE_URL=http://YOUR.VM.PUBLIC.IP
#Environment=PORT=80
# Devices connect to MQTT on this host (defaults to the BASE_URL hostname):
#Environment=MQTT_PUBLIC_HOST=YOUR.VM.PUBLIC.IP

[Service permissions note]
# PORT=80 needs root or: sudo setcap 'cap_net_bind_service=+ep' $(which node)

[Install]
WantedBy=multi-user.target
```

(Remove the `[Service permissions note]` block — it's a comment, not valid
systemd syntax.)

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now eremote
sudo systemctl status eremote     # expect [mqtt] and [http] listening lines
```

## 3. HTTPS with Caddy (skip in IP-only mode)

```bash
sudo apt-get install -y debian-keyring debian-archive-keyring apt-transport-https curl
curl -1sLf https://dl.cloudsmith.io/public/caddy/stable/gpg.key | sudo gpg --dearmor -o /usr/share/keyrings/caddy-stable-archive-keyring.gpg
curl -1sLf https://dl.cloudsmith.io/public/caddy/stable/debian.deb.txt | sudo tee /etc/apt/sources.list.d/caddy-stable.list
sudo apt-get update && sudo apt-get install -y caddy
```

`sudo nano /etc/caddy/Caddyfile` — replace everything with:

```
# Devices claim over plain HTTP (see PROTOCOL.md); Caddy must NOT redirect
# /api/claim to HTTPS because the ESP8266 doesn't follow the redirect.
http://YOUR.DOMAIN {
	@api path /api/*
	reverse_proxy @api 127.0.0.1:8080
	redir https://YOUR.DOMAIN{uri} 308
}

YOUR.DOMAIN {
	reverse_proxy 127.0.0.1:8080
}
```

```bash
sudo systemctl reload caddy
```

Point your domain's A record at the VM's public IP first; Caddy fetches the
Let's Encrypt certificate automatically.

## 4. Flash the firmware

In `ERemote/ERemote.ino` set:

```cpp
const char* ER_HOST = "YOUR.DOMAIN";   // or the raw VM IP in IP-only mode
```

That's the only firmware setting. Devices claim themselves on first
internet connection and show their personal link in the portal's
"Remote access" section.

## 5. Operations

- Device database: `/opt/eremote/server/data.json` (back it up; it maps
  device ids → secrets → codes). State/telemetry is in-memory only.
- A customer lost/leaked their link: delete the device's entry from
  `data.json` (both `devices` and its `codes` entry), restart the service,
  then power-cycle the device — it re-claims and gets a fresh code.
  (`sudo systemctl restart eremote`)
- A replaced/re-flashed board reuses a chip id and gets 403 on claim:
  same fix — delete the stale record and let it re-claim.
- Logs: `journalctl -u eremote -f` (claims and rejects are logged).

## Scaling / hardening later

- The embedded aedes broker comfortably handles thousands of ERemote-class
  devices. If you ever outgrow it, swap in Mosquitto and point the app at
  it — the topic layout and credentials model don't change.
- TLS for MQTT (port 8883) can be added behind the same domain once
  firmware RAM budget is validated; the claim endpoint can move to HTTPS
  at the same time.
