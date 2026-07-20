/* ============================================================================
   ERemote  --  smart scheduling IR remote for split ACs
   Target : WeMos/LOLIN D1 mini (ESP8266, clone OK)   |  Arduino IDE / arduino-cli
   FQBN   : esp8266:esp8266:d1_mini
   Libs   : ArduinoJson (v7), IRremoteESP8266, PubSubClient

   Flow:
     - First boot  -> no /config.json -> serves the step-by-step SETUP WIZARD
       (wizard_html.h): language, record ON/OFF/ECO from the AC remote,
       pick home Wi-Fi from a scanned list, optional genset setup, done.
       Also reachable later from the portal via /?setup=1.
     - Normal boot -> serves the full control portal (portal_html.h, in flash).
     - Always AP+STA: open AP "ERemote" for the phone; STA joins the router
       (entered in the portal) for NTP time + internet.
     - Portal address on the AP: http://4.4.4.4  (chosen because it's short
       and easy to tell customers to type when the captive popup doesn't show).
     - Remote access (IoT): once online, the device claims itself against the
       central server (see server/ and ERemote/PROTOCOL.md), receives a
       personal link https://<host>/r/<CODE>, then keeps an MQTT connection
       to the server so the customer can control it from anywhere via that
       link. Identity (secret + code) lives in EEPROM and survives factory
       reset, so a device keeps the same link for life.
     - Records raw IR frames from the AC remote (reliable for AC protocols)
       and replays them on schedule or on demand.

   Wiring (change the two pin #defines if you moved them):
     IR LED  -> D2 (GPIO4)  via 2N2222: GPIO4 -> ~220R -> base, emitter->GND,
                collector -> LED cathode, LED anode -> +3V3 through ~100R.
     VS1838B -> OUT to D5 (GPIO14), VCC 3V3, GND.
   ============================================================================ */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <EEPROM.h>
#include <ArduinoJson.h>      // v7
#include <PubSubClient.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>
#include <time.h>
#include "portal_html.h"      // PORTAL_HTML[]
#include "wizard_html.h"      // SETUP_HTML[]  (first-run step-by-step wizard)


/* ----------------------------- USER SETTINGS ----------------------------- */
#define IR_TX_PIN   D2        // GPIO4  -> transistor base
#define IR_RX_PIN   D5        // GPIO14 -> VS1838B OUT

const char*  AP_SSID      = "ERemote";
const char*  AP_PASS      = "44448888";    // WPA2; easy default, keeps neighbors out
const uint8_t AP_CHANNEL  = 6;

// ---- Remote access (cloud) ----
// Your server: domain name or public IP of the VPS running server/app.js.
// Used for the one-time claim call and as the default MQTT host.
const char*    ER_HOST            = "er.my.to";
const uint16_t ER_HTTP_PORT       = 80;      // claim endpoint port on ER_HOST
const uint16_t ER_MQTT_PORT       = 1883;
const uint32_t CLAIM_RETRY_MS     = 60000;   // retry claim every minute until it works
const uint32_t MQTT_RETRY_MS      = 30000;   // MQTT reconnect attempt interval
const uint32_t STATE_HEARTBEAT_MS = 60000;   // republish state at least this often

// ESP8266 has ONE radio => ONE TX power for both AP and STA (can't split them).
// 0..20.5 dBm. Lower = shorter range for BOTH. 17 is a balanced default;
// drop toward 10 if you want a weaker AP, but STA range to your router drops too.
const float  WIFI_TX_POWER = 17.0;

const uint32_t BOOT_GRACE_MS     = 8000;   // no *scheduled* send this soon after boot

// AP lifecycle: MANAGE_AP=false keeps the hotspot (and http://4.4.4.4) up
// forever, which is what customers are told to use. Set true to drop the AP
// AP_HOLD_AFTER_STA ms after the router link comes up.
const uint32_t AP_HOLD_AFTER_STA = 180000UL;   // 3 min (only if MANAGE_AP)
const bool     MANAGE_AP         = false;

const uint16_t RECORD_TIMEOUT_MS = 30000;  // matches the wizard's visible countdown

// AutoGenset scanning: a fast single-channel, SSID-filtered probe (~120 ms of
// radio) runs every GS_FAST_INTERVAL_MS, so generator detection reacts within
// ~5 s without disturbing the AP/STA link. Full all-channel sweeps (~2 s) run
// only for the Wi-Fi pickers (/api/scan) or, while the generator is absent,
// every GS_FULL_FALLBACK_MS as a safety net to re-learn the emitter's channel
// (persisted as cfg.gsChannel, learned automatically from full sweeps).
const uint32_t GS_FAST_INTERVAL_MS = 5000;
const uint32_t GS_FULL_FALLBACK_MS = 300000;
/* ------------------------------------------------------------------------- */

// IR capture. (IRremoteESP8266 equivalents of Arduino-IRremote's
// RAW_BUFFER_LENGTH and RECORD_GAP_MICROS, already sized for AC remotes.)
const uint16_t kCaptureBufferSize = 1536;   // timing entries; long AC frames +
                                            // doubled frames (Midea repeats
                                            // with ~5.1ms gap) fit with room
const uint8_t  kTimeout           = 50;     // ms of silence = end of frame;
                                            // above Gree's ~20ms mid-frame gap
                                            // and Midea's inter-frame gap, so
                                            // multi-part frames stay in one
                                            // capture (library max is 130)
const uint8_t  kTolerancePct      = 35;     // %; default 25. Looser matching so
                                            // off-spec remotes (Tossot/clones)
                                            // still get their protocol named
const uint16_t kMinFrameLen       = 40;     // reject shorter bursts while
                                            // recording: repeat codes / noise,
                                            // never a real AC command
IRrecv irrecv(IR_RX_PIN, kCaptureBufferSize, kTimeout, true);
IRsend irsend(IR_TX_PIN);
decode_results results;

// Web
ESP8266WebServer server(80);
DNSServer dnsServer;
const byte DNS_PORT = 53;

// Remote access (MQTT + claim)
WiFiClient   mqttNet;
PubSubClient mqtt(mqttNet);

// Device identity, stored in EEPROM (its flash sector is separate from
// LittleFS, so factory reset / double-reset wipes keep the same identity and
// the customer's link never changes).
const uint32_t ID_MAGIC = 0xE12E1D01;
struct Identity {
  uint32_t magic;
  char     secret[33];   // 32 hex chars, generated on very first boot
  char     code[8];      // 6-char access code assigned by the server
  char     link[96];     // full customer link, e.g. https://host/r/CODE
  char     host[48];     // MQTT host assigned by the server
  uint8_t  claimed;
} ident;
String devId;            // "d" + chip id in hex; MQTT username + topic segment

// Config held in RAM
struct Config {
  String ssid, pass, tz = "Asia/Baghdad"; bool ntp = true;
  // AutoGenset: when a network named gsSsid appears, send gsMode's IR code
  // ("off"/"eco") after gsDelay seconds; "disabled" turns the feature off.
  String gsMode = "disabled", gsSsid = "GENSET_ACTIVE"; uint16_t gsDelay = 3;
  uint8_t gsChannel = 6;   // emitter's Wi-Fi channel; auto-learned from full sweeps
} cfg;

// Runtime state
bool     provisioned   = false;
bool     timeValid     = false;
bool     apOn          = true;
uint32_t bootMillis    = 0;
uint32_t staConnectedAt= 0;
uint32_t staBeginAt    = 0;      // last WiFi.begin(); scans pause while associating
int      lastSchedKey  = -1;

String   recordTarget  = "";     // "on"/"off"/"eco" while capturing
uint32_t recordDeadline= 0;
String   lastCapBtn    = "";     // diagnostics for the portal: last capture's
String   lastCapProto  = "";     // button, detected protocol, pulse count,
uint16_t lastCapLen    = 0;      // and whether the buffer overflowed
bool     lastCapOvf    = false;
String   pendingSend   = "";     // queued transmit (non-blocking delay)
uint32_t sendAt        = 0;

// AutoGenset runtime state
bool     gsPresent     = false;  // generator network currently visible
bool     gsTriggered   = false;  // already fired for this appearance
uint8_t  gsMiss        = 0;      // consecutive scans without the network
uint32_t gsLastScanAt  = 0;

// Shared Wi-Fi scan cache: one scanner feeds both the genset detector and
// the /api/scan network list used by the wizard and portal.
String   scanJson      = "[]";   // [{"n":ssid,"db":rssi,"sec":bool},...]
uint32_t scanDoneAt    = 0;
bool     scanWanted    = false;  // a client asked for a fresh full list
bool     scanFull      = false;  // scan in flight is a full sweep (vs fast probe)
uint32_t lastFullScanAt= 0;

// Remote access runtime state
uint32_t lastClaimAt   = 0;
int      lastClaimRc   = 0;      // 0=never tried; >0 HTTP status; <0 connection error
uint8_t  claimTries    = 0;      // first 3 attempts retry every 10 s, then 60 s
bool     prevWifiConn  = false;
uint32_t lastMqttTry   = 0;
uint32_t lastStatePub  = 0;
bool     statePubQueued= false;  // something changed -> publish state soon

// Double-reset detector (RTC memory survives reset, not power loss)
const uint32_t DRD_MAGIC   = 0xE12E0007;
const uint32_t DRD_TIMEOUT = 5000;
bool     drdCleared    = false;

/* ------------------------------- identity ------------------------------- */
void genSecret(char* out){                 // 32 hex chars + NUL
  for(int i=0;i<4;i++){
    uint32_t r = RANDOM_REG32 ^ micros() ^ (ESP.getChipId()<<i) ^ (ESP.getCycleCount()>>i);
    snprintf(out+i*8, 9, "%08x", r);
    delay(1);
  }
}
void saveIdentity(){ EEPROM.put(0, ident); EEPROM.commit(); }
void loadIdentity(){
  EEPROM.get(0, ident);
  if(ident.magic != ID_MAGIC){             // first boot ever: mint an identity
    memset(&ident, 0, sizeof(ident));
    ident.magic = ID_MAGIC;
    genSecret(ident.secret);
    saveIdentity();
  }
}

/* ------------------------------- helpers -------------------------------- */
bool validBtn(const String& b){ return b=="on"||b=="off"||b=="eco"; }
String irPath(const String& b){ return "/ir_"+b+".json"; }

String posixTZ(const String& tz){
  if (tz == "Asia/Dubai") return "<+04>-4";   // GMT+4
  return "<+03>-3";                            // Baghdad/Kuwait/Riyadh/Istanbul, no DST
}

void applyTZ(){ setenv("TZ", posixTZ(cfg.tz).c_str(), 1); tzset(); }

void applyTime(){
  applyTZ();
  if (cfg.ntp) configTime(posixTZ(cfg.tz).c_str(), "pool.ntp.org", "time.google.com");
}

void saveConfig(){
  JsonDocument d;
  d["ssid"]=cfg.ssid; d["pass"]=cfg.pass; d["tz"]=cfg.tz; d["ntp"]=cfg.ntp;
  d["gsMode"]=cfg.gsMode; d["gsSsid"]=cfg.gsSsid; d["gsDelay"]=cfg.gsDelay;
  d["gsChannel"]=cfg.gsChannel;
  File f=LittleFS.open("/config.json","w"); if(f){ serializeJson(d,f); f.close(); }
}
void loadConfig(){
  File f=LittleFS.open("/config.json","r"); if(!f) return;
  JsonDocument d; if(deserializeJson(d,f)==DeserializationError::Ok){
    cfg.ssid=d["ssid"]|""; cfg.pass=d["pass"]|""; cfg.tz=d["tz"]|"Asia/Baghdad"; cfg.ntp=d["ntp"]|true;
    cfg.gsMode=d["gsMode"]|"disabled"; cfg.gsSsid=d["gsSsid"]|"GENSET_ACTIVE"; cfg.gsDelay=d["gsDelay"]|3;
    cfg.gsChannel=d["gsChannel"]|6;
    provisioned=true;
  }
  f.close();
}

/* ------------------------------- IR store ------------------------------- */
void saveIR(const String& b, const uint16_t* raw, uint16_t len, const String& proto){
  JsonDocument d; d["freq"]=38; d["proto"]=proto;
  JsonArray a=d["raw"].to<JsonArray>();
  for(uint16_t i=0;i<len;i++) a.add(raw[i]);
  File f=LittleFS.open(irPath(b),"w"); if(f){ serializeJson(d,f); f.close(); }
}
bool sendIR(const String& b){
  File f=LittleFS.open(irPath(b),"r"); if(!f) return false;
  JsonDocument d; DeserializationError e=deserializeJson(d,f); f.close();
  if(e) return false;
  JsonArray a=d["raw"].as<JsonArray>(); uint16_t len=a.size(); if(!len) return false;
  uint16_t freq=d["freq"]|38;
  uint16_t* buf=new (std::nothrow) uint16_t[len]; if(!buf) return false;
  uint16_t i=0; for(JsonVariant v:a) buf[i++]=v.as<uint16_t>();
  irsend.sendRaw(buf,len,freq);
  delete[] buf; return true;
}

/* ------------------------------ schedules ------------------------------- */
void readSched(JsonDocument& d){
  File f=LittleFS.open("/sched.json","r");
  if(!f || deserializeJson(d,f)){ d.to<JsonArray>(); }   // default []
  if(f) f.close();
  if(!d.is<JsonArray>()) d.to<JsonArray>();
}
void writeSched(JsonDocument& d){
  File f=LittleFS.open("/sched.json","w"); if(f){ serializeJson(d,f); f.close(); }
}

/* ------------------------------- WiFi ----------------------------------- */
void startAP(){
  // Explicit AP addressing: guarantees the DHCP server advertises us as
  // gateway AND DNS server, which the captive-portal hijack depends on.
  // 4.4.4.4 on purpose: short enough to tell customers to type into their
  // browser when the automatic captive popup doesn't appear.
  IPAddress apIP(4,4,4,4);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255,255,255,0));
  WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL);
  dnsServer.setTTL(0);                          // don't let phones cache answers
  dnsServer.start(DNS_PORT, "*", apIP);         // every hostname -> us
  apOn=true;
}
void connectSTA(){
  if(cfg.ssid.length()){
    WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
    staConnectedAt=0; staBeginAt=millis();
  }
}
void manageAP(){
  if(!MANAGE_AP) return;
  if(WiFi.status()==WL_CONNECTED){
    if(staConnectedAt==0) staConnectedAt=millis();
    if(apOn && millis()-staConnectedAt>AP_HOLD_AFTER_STA){ dnsServer.stop(); WiFi.softAPdisconnect(true); apOn=false; }
  } else {
    staConnectedAt=0;
    if(!apOn){ startAP(); }   // router lost -> bring AP back so the portal stays reachable
  }
}

/* ------------------------- captive portal probes -------------------------
   Phones decide whether to pop the "sign in to network" page by fetching a
   known URL and checking the answer (Android expects HTTP 204, Apple expects
   the word "Success", Windows expects "Microsoft NCSI"). Our DNS hijack sends
   those requests here; answering with a redirect to our own IP (and closing
   the socket, which some Android builds require) triggers the portal popup. */
void redirectToSelf(){
  // Redirect to the IP of whichever interface (AP or STA) the client used.
  IPAddress ip = server.client().localIP();
  server.sendHeader("Location", String("http://") + ip.toString() + "/", true);
  server.send(302, "text/html", "");
  server.client().stop();
}
bool isSelfHost(){
  String h = server.hostHeader();
  int c = h.indexOf(':'); if(c >= 0) h = h.substring(0, c);   // strip :port
  return h == server.client().localIP().toString() || h == "eremote" || h == "eremote.local";
}

/* ============================ HTTP handlers ============================== */
void sendJson(int code, const String& body){ server.send(code,"application/json",body); }
bool bodyJson(JsonDocument& d){ return server.hasArg("plain") && !deserializeJson(d, server.arg("plain")); }

void handleRoot(){
  // Wizard on first run, or on demand via the portal's "run setup wizard".
  bool wizard = !provisioned || server.hasArg("setup");
  server.send_P(200, "text/html", wizard ? SETUP_HTML : PORTAL_HTML);
}

void handleScan(){
  scanWanted=true;                       // ask scanTask for a fresh sweep
  String out = "{\"scanning\":";
  out += (WiFi.scanComplete()==WIFI_SCAN_RUNNING) ? "true" : "false";
  out += ",\"age\":";
  out += scanDoneAt ? String(millis()-scanDoneAt) : String(-1);
  out += ",\"networks\":" + scanJson + "}";
  sendJson(200,out);
}

void handleInit(){                       // first-boot provisioning
  if(!LittleFS.exists("/config.json")) saveConfig();
  if(!LittleFS.exists("/sched.json")){ JsonDocument d; d.to<JsonArray>(); writeSched(d); }
  provisioned=true;
  sendJson(200, "{\"ok\":true}");
}

void handleStatus(){
  JsonDocument d;
  d["codes"]["on"]["set"]  = LittleFS.exists(irPath("on"));
  d["codes"]["off"]["set"] = LittleFS.exists(irPath("off"));
  d["codes"]["eco"]["set"] = LittleFS.exists(irPath("eco"));

  bool c = WiFi.status()==WL_CONNECTED;
  d["wifi"]["connected"]=c;
  d["wifi"]["ssid"]=cfg.ssid;
  d["wifi"]["ip"]  = c ? WiFi.localIP().toString() : "";
  d["wifi"]["rssi"]= c ? WiFi.RSSI() : 0;

  d["time"]["valid"]=timeValid;
  d["time"]["epoch"]=(uint32_t)time(nullptr);
  d["time"]["ntp"]=cfg.ntp;
  d["time"]["tz"]=cfg.tz;

  d["genset"]["mode"]=cfg.gsMode;
  d["genset"]["ssid"]=cfg.gsSsid;
  d["genset"]["delay"]=cfg.gsDelay;
  d["genset"]["detected"]=gsPresent;

  d["lastCapture"]["btn"]=lastCapBtn;
  d["lastCapture"]["proto"]=lastCapProto;
  d["lastCapture"]["len"]=lastCapLen;
  d["lastCapture"]["overflow"]=lastCapOvf;

  d["remote"]["claimed"]=(bool)ident.claimed;
  d["remote"]["code"]=ident.code;
  d["remote"]["link"]=ident.link;
  d["remote"]["mqtt"]=mqtt.connected();
  d["remote"]["lastRc"]=lastClaimRc;

  JsonDocument sd; readSched(sd);
  d["schedules"]=sd.as<JsonArray>();

  String out; serializeJson(d,out); sendJson(200,out);
}

void handleRecord(){
  String b=server.arg("btn");
  if(!validBtn(b)){ sendJson(400,"{\"ok\":false}"); return; }
  recordTarget=b; recordDeadline=millis()+RECORD_TIMEOUT_MS;
  irrecv.resume();                       // clear any stale frame
  sendJson(200,"{\"ok\":true}");
}

void handleSend(){
  String b=server.arg("btn");
  if(!validBtn(b) || !LittleFS.exists(irPath(b))){ sendJson(400,"{\"ok\":false}"); return; }
  pendingSend=b; sendAt=millis();          // immediate; loop() fires it
  sendJson(200, "{\"ok\":true}");
}

void handleWifiSave(){
  JsonDocument d; if(!bodyJson(d)){ sendJson(400,"{\"ok\":false}"); return; }
  cfg.ssid=(const char*)(d["ssid"]|""); cfg.pass=(const char*)(d["pass"]|"");
  saveConfig(); connectSTA();
  sendJson(200,"{\"ok\":true}");
}
void handleWifiForget(){
  cfg.ssid=""; cfg.pass=""; saveConfig(); WiFi.disconnect(); staConnectedAt=0;
  sendJson(200,"{\"ok\":true}");
}

void setManualTime(const char* iso){
  int y,mo,dd,h,mi; if(sscanf(iso,"%d-%d-%dT%d:%d",&y,&mo,&dd,&h,&mi)<5) return;
  applyTZ();
  struct tm tm={}; tm.tm_year=y-1900; tm.tm_mon=mo-1; tm.tm_mday=dd; tm.tm_hour=h; tm.tm_min=mi;
  time_t t=mktime(&tm); struct timeval tv={t,0}; settimeofday(&tv,nullptr);
  timeValid=true;
}
void handleTime(){
  JsonDocument d; if(!bodyJson(d)){ sendJson(400,"{\"ok\":false}"); return; }
  cfg.ntp=d["ntp"]|true; cfg.tz=(const char*)(d["tz"]|"Asia/Baghdad"); saveConfig();
  if(cfg.ntp){ timeValid=false; applyTime(); }
  else if(d["iso"].is<const char*>()) setManualTime(d["iso"]);
  sendJson(200,"{\"ok\":true}");
}

void handleGenset(){
  JsonDocument d; if(!bodyJson(d)){ sendJson(400,"{\"ok\":false}"); return; }
  String m=(const char*)(d["mode"]|"disabled");
  cfg.gsMode=(m=="off"||m=="eco") ? m : "disabled";
  cfg.gsDelay=d["delay"]|3;
  cfg.gsSsid=(const char*)(d["ssid"]|"GENSET_ACTIVE");
  if(!cfg.gsSsid.length()) cfg.gsSsid="GENSET_ACTIVE";
  saveConfig();
  gsPresent=false; gsTriggered=false; gsMiss=0;   // re-arm with new settings
  sendJson(200,"{\"ok\":true}");
}

void handleSchedGet(){
  File f=LittleFS.open("/sched.json","r");
  if(!f){ sendJson(200,"[]"); return; }
  server.streamFile(f,"application/json"); f.close();
}
void handleSchedPost(){
  JsonDocument in; if(!bodyJson(in)){ sendJson(400,"{\"ok\":false}"); return; }
  JsonDocument arr; readSched(arr); JsonArray a=arr.as<JsonArray>();
  uint32_t id = in["id"].is<uint32_t>() ? (uint32_t)in["id"] : (uint32_t)(millis());
  JsonObject o=a.add<JsonObject>();
  o["id"]=id; o["action"]=(const char*)(in["action"]|"on");
  o["hour"]=(int)(in["hour"]|0); o["min"]=(int)(in["min"]|0);
  JsonArray days=o["days"].to<JsonArray>();
  for(JsonVariant v: in["days"].as<JsonArray>()) days.add((int)v);
  writeSched(arr);
  sendJson(200, "{\"ok\":true,\"id\":"+String(id)+"}");
}
void handleSchedDel(){
  if(!server.hasArg("id")){ sendJson(400,"{\"ok\":false}"); return; }
  uint32_t id=strtoul(server.arg("id").c_str(),nullptr,10);
  JsonDocument arr; readSched(arr); JsonArray a=arr.as<JsonArray>();
  for(size_t i=0;i<a.size();i++){ if((uint32_t)a[i]["id"]==id){ a.remove(i); break; } }
  writeSched(arr);
  sendJson(200,"{\"ok\":true}");
}

void handleReset(){
  sendJson(200,"{\"ok\":true}");
  delay(150); LittleFS.format(); ESP.restart();
}

void handleNotFound(){
  if(!isSelfHost()){ redirectToSelf(); return; }   // captive portal: bounce home
  server.send(404, "text/plain", "Not found");
}

/* ------------------------------- loop bits ------------------------------ */
void captureIR(){
  if(irrecv.decode(&results)){
    if(recordTarget!=""){
      uint16_t len=getCorrectedRawLength(&results);
      if(results.overflow){
        // Truncated frame would replay garbage: report it, keep listening.
        lastCapBtn=recordTarget; lastCapProto=""; lastCapLen=len; lastCapOvf=true;
      } else if(len<kMinFrameLen){
        // Stray repeat code or noise burst, not an AC command: keep listening.
      } else {
        uint16_t* raw=resultToRawArray(&results);
        if(raw){
          String proto=typeToString(results.decode_type);
          saveIR(recordTarget,raw,len,proto); delete[] raw;
          lastCapBtn=recordTarget; lastCapProto=proto; lastCapLen=len; lastCapOvf=false;
          recordTarget="";
          statePubQueued=true;                 // tell the server a code changed
        }
      }
    }
    irrecv.resume();
  }
  if(recordTarget!="" && (int32_t)(millis()-recordDeadline)>0) recordTarget=""; // timed out
}

void firePending(){
  if(pendingSend!="" && (int32_t)(millis()-sendAt)>=0){
    String b=pendingSend; pendingSend="";
    sendIR(b);
  }
}

void checkSchedules(){
  if(!timeValid) return;
  if(millis()-bootMillis < BOOT_GRACE_MS) return;
  time_t now=time(nullptr); struct tm* lt=localtime(&now);
  int key=lt->tm_hour*60+lt->tm_min;
  if(key==lastSchedKey) return;           // run at most once per minute
  lastSchedKey=key;

  JsonDocument arr; readSched(arr);
  for(JsonObject s: arr.as<JsonArray>()){
    if((int)s["hour"]!=lt->tm_hour || (int)s["min"]!=lt->tm_min) continue;
    bool today=false; for(JsonVariant v:s["days"].as<JsonArray>()) if((int)v==lt->tm_wday){ today=true; break; }
    if(!today) continue;
    String b=(const char*)(s["action"]|"on");   // "on"/"off"
    if(LittleFS.exists(irPath(b))){ pendingSend=b; sendAt=millis(); }
  }
}

void gensetSeen(bool found){
  // Trigger/re-arm state machine. Detection always runs so the indicator
  // LED works even when the automatic action is disabled; only the IR send
  // is gated on the mode.
  bool armed = (cfg.gsMode=="off" || cfg.gsMode=="eco");
  if(found){
    gsMiss=0;
    if(!gsPresent){                                  // generator just came on
      gsPresent=true; statePubQueued=true;
      if(armed && !gsTriggered && LittleFS.exists(irPath(cfg.gsMode))){
        pendingSend=cfg.gsMode;
        sendAt=millis()+cfg.gsDelay*1000UL;
        gsTriggered=true;
      }
    }
  } else if(gsPresent && ++gsMiss>=2){               // gone for 2 scans -> re-arm
    gsPresent=false; gsTriggered=false; gsMiss=0; statePubQueued=true;
  }
}

void scanTask(){
  // Single owner of the Wi-Fi scanner. Two scan types:
  //  - fast probe: async, single channel (cfg.gsChannel), filtered to the
  //    genset SSID; ~120 ms of radio, runs every GS_FAST_INTERVAL_MS.
  //  - full sweep: all channels, feeds the /api/scan picker list AND learns
  //    the emitter's channel; run on demand or as a rare absence fallback.
  int n=WiFi.scanComplete();
  if(n>=0){                                          // a scan just finished
    bool found=false;
    if(scanFull){
      JsonDocument d; JsonArray a=d.to<JsonArray>();
      for(int i=0;i<n;i++){
        String ss=WiFi.SSID(i);
        if(ss==cfg.gsSsid){
          found=true;
          uint8_t ch=WiFi.channel(i);                // auto-learn emitter channel
          if(ch && ch!=cfg.gsChannel){ cfg.gsChannel=ch; saveConfig(); }
        }
        if(!ss.length()) continue;                   // hidden networks
        bool dup=false;
        for(JsonObject o:a) if(ss==(const char*)o["n"]){
          dup=true;
          if((int)o["db"]<WiFi.RSSI(i)) o["db"]=WiFi.RSSI(i);
          break;
        }
        if(dup || a.size()>=30) continue;
        JsonObject o=a.add<JsonObject>();
        o["n"]=ss; o["db"]=WiFi.RSSI(i);
        o["sec"]=(WiFi.encryptionType(i)!=ENC_TYPE_NONE);
      }
      scanJson=""; serializeJson(d,scanJson);
      scanDoneAt=millis(); scanWanted=false; lastFullScanAt=millis();
    } else {
      for(int i=0;i<n;i++) if(WiFi.SSID(i)==cfg.gsSsid){ found=true; break; }
    }
    WiFi.scanDelete();
    gensetSeen(found);
    return;
  }
  if(n==WIFI_SCAN_RUNNING) return;

  bool associating = cfg.ssid.length() && WiFi.status()!=WL_CONNECTED &&
                     staBeginAt && millis()-staBeginAt<25000;

  // Full sweeps hop all channels for ~2 s, so never during association.
  if(scanWanted && !associating){
    scanFull=true; gsLastScanAt=millis();
    WiFi.scanNetworks(true, true);
    return;
  }
  if(millis()-gsLastScanAt<GS_FAST_INTERVAL_MS) return;
  gsLastScanAt=millis();
  if(!gsPresent && !associating &&
     millis()-lastFullScanAt>GS_FULL_FALLBACK_MS){
    scanFull=true;                                   // re-learn channel safety net
    WiFi.scanNetworks(true, true);
  } else {
    scanFull=false;                                  // 120 ms probe, always safe
    WiFi.scanNetworks(true, false, cfg.gsChannel, (uint8_t*)cfg.gsSsid.c_str());
  }
}

/* --------------------------- remote access ------------------------------ */
/* One-time claim: register this device with the central server and receive
   the customer link + MQTT host. Plain HTTP on purpose: MQTT itself runs
   without TLS in v1, so the device password crosses the wire on every MQTT
   connect anyway — TLS on just the claim would add BearSSL's ~25 KB RAM
   spike without a real security gain. The server refuses a known id with a
   different secret, so the link can't be hijacked. */
void claimTask(){
  bool conn = WiFi.status()==WL_CONNECTED;
  if(conn && !prevWifiConn){ lastClaimAt=0; claimTries=0; }  // claim right away
  prevWifiConn=conn;
  if(ident.claimed || !conn) return;
  // Quick retries first (server hiccup, DNS warm-up), then back off.
  uint32_t interval = (claimTries<3) ? 10000 : CLAIM_RETRY_MS;
  if(lastClaimAt && millis()-lastClaimAt<interval) return;
  lastClaimAt=millis(); claimTries++;

  WiFiClient net; HTTPClient http;
  http.setTimeout(8000);
  if(!http.begin(net, ER_HOST, ER_HTTP_PORT, "/api/claim")) return;
  http.addHeader("Content-Type","application/json");
  JsonDocument d; d["id"]=devId; d["secret"]=ident.secret; d["fw"]="1.0";
  String body; serializeJson(d,body);
  int rc=http.POST(body);
  lastClaimRc=rc;                  // negative = HTTPClient error (unreachable etc.)
  Serial.printf("claim %s:%u -> %d\n", ER_HOST, ER_HTTP_PORT, rc);
  if(rc==200){
    JsonDocument r;
    if(deserializeJson(r,http.getString())==DeserializationError::Ok && (r["ok"]|false)){
      strlcpy(ident.code, (const char*)(r["code"]|""),         sizeof(ident.code));
      strlcpy(ident.link, (const char*)(r["link"]|""),         sizeof(ident.link));
      strlcpy(ident.host, (const char*)(r["mqtt"]["host"]|ER_HOST), sizeof(ident.host));
      if(ident.code[0]){ ident.claimed=1; saveIdentity(); }
    }
  }
  http.end();
}

String stateTopic(){ return "er/"+devId+"/state"; }
String cmdTopic()  { return "er/"+devId+"/cmd";  }

void mqttCallback(char* topic, byte* payload, unsigned int len){
  (void)topic;
  String p; p.reserve(len);
  for(unsigned int i=0;i<len;i++) p+=(char)payload[i];
  String b=p;
  if(p.indexOf('{')>=0){ JsonDocument d; if(!deserializeJson(d,p)) b=(const char*)(d["btn"]|""); }
  b.trim();
  if(validBtn(b) && LittleFS.exists(irPath(b))){
    pendingSend=b; sendAt=millis();
    statePubQueued=true;
  }
}

void publishState(){
  if(!mqtt.connected()) return;
  JsonDocument d;
  d["online"]=true;
  d["codes"]["on"] =LittleFS.exists(irPath("on"));
  d["codes"]["off"]=LittleFS.exists(irPath("off"));
  d["codes"]["eco"]=LittleFS.exists(irPath("eco"));
  d["genset"]=gsPresent;
  d["rssi"]=WiFi.RSSI();
  String out; serializeJson(d,out);
  mqtt.publish(stateTopic().c_str(), out.c_str(), true);   // retained
  lastStatePub=millis(); statePubQueued=false;
}

void mqttTask(){
  if(!ident.claimed || WiFi.status()!=WL_CONNECTED) return;
  if(!mqtt.connected()){
    if(lastMqttTry && millis()-lastMqttTry<MQTT_RETRY_MS) return;
    lastMqttTry=millis();
    mqtt.setServer(ident.host[0] ? ident.host : ER_HOST, ER_MQTT_PORT);
    mqtt.setCallback(mqttCallback);
    // LWT: broker marks us offline (retained) if the link dies.
    if(mqtt.connect(devId.c_str(), devId.c_str(), ident.secret,
                    stateTopic().c_str(), 0, true, "{\"online\":false}")){
      mqtt.subscribe(cmdTopic().c_str());
      publishState();
    }
    return;
  }
  mqtt.loop();
  if(statePubQueued || millis()-lastStatePub>STATE_HEARTBEAT_MS) publishState();
}

/* --------------------------------- DRD ---------------------------------- */
void handleDRD(){
  uint32_t flag=0; ESP.rtcUserMemoryRead(0,&flag,sizeof(flag));
  if(flag==DRD_MAGIC){                      // second reset within window -> wipe
    LittleFS.format();
    flag=0; ESP.rtcUserMemoryWrite(0,&flag,sizeof(flag));
  } else {
    flag=DRD_MAGIC; ESP.rtcUserMemoryWrite(0,&flag,sizeof(flag)); // arm for a few seconds
  }
}
void clearDRD(){ if(!drdCleared && millis()>DRD_TIMEOUT){ uint32_t z=0; ESP.rtcUserMemoryWrite(0,&z,sizeof(z)); drdCleared=true; } }

/* --------------------------------- setup -------------------------------- */
void setup(){
  bootMillis=millis();
  Serial.begin(115200); Serial.println(F("\nERemote booting"));

  if(!LittleFS.begin()){ LittleFS.format(); LittleFS.begin(); }
  handleDRD();               // may format on double-tap RST
  loadConfig();              // sets provisioned + cfg

  EEPROM.begin(sizeof(Identity));
  loadIdentity();            // mints the device secret on very first boot
  devId = "d" + String(ESP.getChipId(), HEX);
  mqtt.setBufferSize(512);

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.hostname("ERemote");
  WiFi.setOutputPower(WIFI_TX_POWER);   // single radio: applies to AP and STA
  startAP();                 // also starts the captive-portal DNS hijack

  // Fast-boot genset check: one short blocking probe (~150 ms) BEFORE the
  // router association starts, so a device powering up on generator supply
  // queues its OFF/ECO command within seconds of getting power.
  {
    int bn=WiFi.scanNetworks(false, false, cfg.gsChannel,
                             (uint8_t*)cfg.gsSsid.c_str());
    bool bfound=false;
    for(int i=0;i<bn;i++) if(WiFi.SSID(i)==cfg.gsSsid){ bfound=true; break; }
    WiFi.scanDelete();
    if(bfound) gensetSeen(true);
    gsLastScanAt=millis();
  }

  connectSTA();

  irsend.begin();
  irrecv.setTolerance(kTolerancePct);
  irrecv.enableIRIn();

  applyTime();

  // routes
  server.on("/", handleRoot);

  // OS connectivity-check URLs -> redirect so the phone pops the portal.
  static const char* probes[] = {
    "/generate_204", "/gen_204",                       // Android
    "/hotspot-detect.html", "/library/test/success.html", // iOS/macOS
    "/connecttest.txt", "/ncsi.txt", "/redirect", "/fwlink", // Windows
    "/success.txt", "/canonical.html"                  // Firefox
  };
  for(auto p : probes) server.on(p, redirectToSelf);
  server.on("/api/init",      HTTP_POST,   handleInit);
  server.on("/api/status",    HTTP_GET,    handleStatus);
  server.on("/api/scan",      HTTP_GET,    handleScan);
  server.on("/api/record",    HTTP_POST,   handleRecord);
  server.on("/api/send",      HTTP_POST,   handleSend);
  server.on("/api/wifi",      HTTP_POST,   handleWifiSave);
  server.on("/api/wifi",      HTTP_DELETE, handleWifiForget);
  server.on("/api/time",      HTTP_POST,   handleTime);
  server.on("/api/genset",    HTTP_POST,   handleGenset);
  server.on("/api/schedule",  HTTP_GET,    handleSchedGet);
  server.on("/api/schedule",  HTTP_POST,   handleSchedPost);
  server.on("/api/schedule",  HTTP_DELETE, handleSchedDel);
  server.on("/api/factory-reset", HTTP_POST, handleReset);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.printf("AP: %s  IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
}

/* --------------------------------- loop --------------------------------- */
void loop(){
  if(apOn) dnsServer.processNextRequest();
  server.handleClient();
  captureIR();
  firePending();
  if(!timeValid && time(nullptr)>100000) timeValid=true;
  manageAP();
  checkSchedules();
  scanTask();
  claimTask();
  mqttTask();
  clearDRD();
}
