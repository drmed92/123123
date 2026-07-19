/*
 * ERemote — WiFi remote control with captive-portal provisioning.
 *
 * First boot (not provisioned): starts an access point "ERemote-Setup" and
 * serves SETUP_HTML, a captive portal page where you enter your WiFi
 * credentials. They are saved to EEPROM and the board reboots.
 *
 * Provisioned: connects to your WiFi and serves PORTAL_HTML, the remote
 * control UI. Buttons call /cmd?b=<name>; wire the commands to your
 * relays / IR emitter in runCommand() below.
 *
 * Visit /reset to clear saved credentials and return to setup mode.
 *
 * Works on ESP8266 and ESP32.
 */

#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266WebServer.h>
  ESP8266WebServer server(80);
#elif defined(ESP32)
  #include <WiFi.h>
  #include <WebServer.h>
  WebServer server(80);
#else
  #error "This sketch requires an ESP8266 or ESP32 board."
#endif

#include <DNSServer.h>
#include <EEPROM.h>

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
const char *AP_SSID       = "ERemote-Setup";
const byte  DNS_PORT      = 53;
const int   EEPROM_SIZE   = 160;
const byte  MAGIC         = 0x42;          // marks EEPROM as containing creds
const int   RELAY_PIN     = 2;             // adjust to your wiring
const unsigned long WIFI_TIMEOUT_MS = 20000;

DNSServer dnsServer;
bool provisioned = false;
char savedSsid[33];
char savedPass[65];

// ---------------------------------------------------------------------------
// HTML pages (must be defined before handleRoot uses them)
// ---------------------------------------------------------------------------

// Setup / provisioning page — shown while in AP mode.
const char SETUP_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ERemote Setup</title>
<style>
  body{font-family:system-ui,sans-serif;background:#111;color:#eee;
       display:flex;justify-content:center;align-items:center;min-height:100vh;margin:0}
  .card{background:#1d1d1d;padding:28px;border-radius:12px;width:min(320px,90vw);
        box-shadow:0 4px 20px rgba(0,0,0,.5)}
  h1{font-size:1.3rem;margin:0 0 6px}
  p{color:#999;font-size:.85rem;margin:0 0 18px}
  label{display:block;font-size:.8rem;color:#bbb;margin-bottom:4px}
  input{width:100%;box-sizing:border-box;padding:10px;margin-bottom:14px;
        border:1px solid #333;border-radius:8px;background:#111;color:#eee;font-size:1rem}
  button{width:100%;padding:12px;border:0;border-radius:8px;background:#2e7d32;
         color:#fff;font-size:1rem;cursor:pointer}
  button:active{background:#1b5e20}
</style>
</head>
<body>
  <div class="card">
    <h1>ERemote Setup</h1>
    <p>Connect this device to your WiFi network.</p>
    <form action="/save" method="POST">
      <label for="ssid">Network name (SSID)</label>
      <input id="ssid" name="ssid" maxlength="32" required>
      <label for="pass">Password</label>
      <input id="pass" name="pass" type="password" maxlength="64">
      <button type="submit">Save &amp; Connect</button>
    </form>
  </div>
</body>
</html>
)rawliteral";

// Remote control page — shown once connected to WiFi.
const char PORTAL_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ERemote</title>
<style>
  body{font-family:system-ui,sans-serif;background:#111;color:#eee;
       display:flex;justify-content:center;align-items:center;min-height:100vh;margin:0}
  .card{background:#1d1d1d;padding:28px;border-radius:12px;width:min(320px,90vw);
        box-shadow:0 4px 20px rgba(0,0,0,.5);text-align:center}
  h1{font-size:1.3rem;margin:0 0 18px}
  .grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}
  button{padding:16px 0;border:0;border-radius:10px;background:#2a2a2a;color:#eee;
         font-size:1rem;cursor:pointer}
  button:active{background:#444}
  .power{grid-column:1/-1;background:#b71c1c}
  .power:active{background:#7f0000}
  #status{margin-top:16px;font-size:.8rem;color:#888;min-height:1em}
</style>
</head>
<body>
  <div class="card">
    <h1>ERemote</h1>
    <div class="grid">
      <button class="power" onclick="cmd('power')">Power</button>
      <button onclick="cmd('up')">▲</button>
      <button onclick="cmd('down')">▼</button>
      <button onclick="cmd('on')">On</button>
      <button onclick="cmd('off')">Off</button>
    </div>
    <div id="status"></div>
  </div>
<script>
function cmd(b){
  fetch('/cmd?b='+b)
    .then(r=>r.text())
    .then(t=>{document.getElementById('status').textContent=t;})
    .catch(()=>{document.getElementById('status').textContent='Error';});
}
</script>
</body>
</html>
)rawliteral";

// ---------------------------------------------------------------------------
// Credential storage
// ---------------------------------------------------------------------------
bool loadCredentials() {
  if (EEPROM.read(0) != MAGIC) return false;
  for (int i = 0; i < 32; i++) savedSsid[i] = EEPROM.read(1 + i);
  savedSsid[32] = '\0';
  for (int i = 0; i < 64; i++) savedPass[i] = EEPROM.read(33 + i);
  savedPass[64] = '\0';
  return savedSsid[0] != '\0';
}

void saveCredentials(const String &ssid, const String &pass) {
  EEPROM.write(0, MAGIC);
  for (int i = 0; i < 32; i++) EEPROM.write(1 + i, i < (int)ssid.length() ? ssid[i] : 0);
  for (int i = 0; i < 64; i++) EEPROM.write(33 + i, i < (int)pass.length() ? pass[i] : 0);
  EEPROM.commit();
}

void clearCredentials() {
  EEPROM.write(0, 0);
  EEPROM.commit();
}

// ---------------------------------------------------------------------------
// Commands — wire these to your hardware
// ---------------------------------------------------------------------------
String runCommand(const String &b) {
  if (b == "power") {
    digitalWrite(RELAY_PIN, !digitalRead(RELAY_PIN));
    return "Power toggled";
  }
  if (b == "on")   { digitalWrite(RELAY_PIN, HIGH); return "On"; }
  if (b == "off")  { digitalWrite(RELAY_PIN, LOW);  return "Off"; }
  if (b == "up")   { /* TODO: e.g. send IR volume-up */ return "Up"; }
  if (b == "down") { /* TODO: e.g. send IR volume-down */ return "Down"; }
  return "Unknown command";
}

// ---------------------------------------------------------------------------
// HTTP handlers
// ---------------------------------------------------------------------------
void handleRoot() {
  server.send_P(200, "text/html", provisioned ? PORTAL_HTML : SETUP_HTML);
}

void handleSave() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  if (ssid.length() == 0) {
    server.send(400, "text/plain", "SSID is required");
    return;
  }
  saveCredentials(ssid, pass);
  server.send(200, "text/html",
              "<html><body style='font-family:sans-serif'>"
              "<h2>Saved. Rebooting&hellip;</h2>"
              "<p>ERemote will now connect to your network.</p>"
              "</body></html>");
  delay(1500);
  ESP.restart();
}

void handleCmd() {
  if (!provisioned) { server.send(403, "text/plain", "Not provisioned"); return; }
  server.send(200, "text/plain", runCommand(server.arg("b")));
}

void handleReset() {
  clearCredentials();
  server.send(200, "text/plain", "Credentials cleared. Rebooting into setup mode...");
  delay(1500);
  ESP.restart();
}

void handleNotFound() {
  if (!provisioned) {
    // Captive portal: redirect every unknown request to the setup page.
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "Not found");
  }
}

// ---------------------------------------------------------------------------
// Setup / loop
// ---------------------------------------------------------------------------
void startSetupMode() {
  provisioned = false;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  Serial.print(F("Setup mode. Join AP '"));
  Serial.print(AP_SSID);
  Serial.print(F("' and open http://"));
  Serial.println(WiFi.softAPIP());
}

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  EEPROM.begin(EEPROM_SIZE);

  if (loadCredentials()) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(savedSsid, savedPass);
    Serial.print(F("Connecting to "));
    Serial.println(savedSsid);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
      delay(250);
      Serial.print('.');
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      provisioned = true;
      Serial.print(F("Connected. Open http://"));
      Serial.println(WiFi.localIP());
    } else {
      Serial.println(F("Connection failed — falling back to setup mode."));
      startSetupMode();
    }
  } else {
    startSetupMode();
  }

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/cmd", handleCmd);
  server.on("/reset", handleReset);
  server.onNotFound(handleNotFound);
  server.begin();
}

void loop() {
  if (!provisioned) dnsServer.processNextRequest();
  server.handleClient();
}
