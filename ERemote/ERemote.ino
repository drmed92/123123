/* ============================================================================
   ERemote  --  smart scheduling IR remote for split ACs
   Target : WeMos/LOLIN D1 mini (ESP8266, clone OK)   |  Arduino IDE / arduino-cli
   FQBN   : esp8266:esp8266:d1_mini
   Libs   : ArduinoJson (v7), IRremoteESP8266   (see install_libs.py)

   Flow:
     - First boot  -> no /config.json -> serves the embedded SETUP WIZARD.
       User taps "Initialize" -> device creates its data files -> main portal.
     - Normal boot -> serves the full control portal (portal_html.h, in flash).
     - Always AP+STA: open AP "ERemote" for the phone; STA joins the router
       (entered in the portal) for NTP time + internet.
     - Records raw IR frames from the AC remote (reliable for AC protocols)
       and replays them on schedule or on demand.

   Wiring (change the two pin #defines if you moved them):
     IR LED  -> D2 (GPIO4)  via 2N2222: GPIO4 -> ~220R -> base, emitter->GND,
                collector -> LED cathode, LED anode -> +3V3 through ~100R.
     VS1838B -> OUT to D5 (GPIO14), VCC 3V3, GND.
   ============================================================================ */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>      // v7
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>
#include <time.h>
#include "portal_html.h"      // PORTAL_HTML[]

/* ===================== embedded first-boot setup wizard ==================
   NOTE: this must live ABOVE handleRoot(). The Arduino builder auto-generates
   forward prototypes for functions only, never for variables, so a global
   defined below its first use fails with "not declared in this scope". */
const char SETUP_HTML[] PROGMEM = R"SETUP(
<!DOCTYPE html><html lang="ar" dir="rtl"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ERemote setup</title><style>
:root{--b:#0f766e;--b2:#0ea5a4}*{box-sizing:border-box}
body{margin:0;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Tahoma,sans-serif;
background:#0b1220;color:#e6edf5;display:grid;place-items:center;min-height:100vh;padding:24px}
.card{max-width:380px;width:100%;background:#131c2b;border:1px solid #243244;border-radius:20px;
padding:28px;text-align:center;box-shadow:0 10px 30px rgba(0,0,0,.4);position:relative}
.logo{width:56px;height:56px;border-radius:16px;margin:0 auto 14px;
background:linear-gradient(135deg,var(--b),var(--b2));display:grid;place-items:center}
.logo svg{width:30px;height:30px;color:#fff}
h1{font-size:20px;margin:0 0 6px}p{color:#93a1b5;font-size:14px;line-height:1.6;margin:0 0 22px}
button{border:0;border-radius:14px;padding:14px;font-size:15px;font-weight:700;
color:#fff;background:var(--b);cursor:pointer;font-family:inherit}
#go{width:100%}
button:disabled{opacity:.6}.ok{color:#86efac;font-weight:600;margin-top:14px;display:none}
.lang{position:absolute;top:14px;inset-inline-end:14px;padding:6px 14px;font-size:13px;
font-weight:600;background:#243244;border-radius:10px}
</style></head><body><div class="card">
<button class="lang" id="lang" onclick="setLang(L=='en'?'ar':'en')">عربي</button>
<div class="logo"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"
stroke-linecap="round"><path d="M4 6h16v9H4z"/><path d="M8 19h8M12 15v4"/><circle cx="8" cy="10.5" r="1"/>
<path d="M12 9v3M16 9v3"/></svg></div>
<h1 id="t1"></h1>
<p id="t2"></p>
<button id="go" onclick="init()"></button>
<div class="ok" id="ok"></div>
</div><script>
var D={en:{t1:'Welcome to ERemote',
t2:"Let's set up your smart AC remote. This creates the device's storage so your codes, Wi-Fi and schedules survive power loss.",
go:'Initialize device',busy:'Setting up…',ok:'Ready! Loading portal…',lang:'عربي'},
ar:{t1:'أهلاً بك في ERemote',
t2:'لنقم بإعداد جهاز التحكم الذكي بالمكيف. هذه الخطوة تُنشئ ذاكرة الجهاز حتى تبقى الأكواد وشبكة الواي فاي والجداول محفوظة بعد انقطاع الكهرباء.',
go:'تهيئة الجهاز',busy:'جارٍ الإعداد…',ok:'تم! جارٍ فتح لوحة التحكم…',lang:'EN'}};
var L='ar';
function setLang(l){L=l;try{localStorage.setItem('erl',l)}catch(e){}
document.documentElement.lang=l;document.documentElement.dir=(l=='ar')?'rtl':'ltr';
var d=D[l];t1.textContent=d.t1;t2.textContent=d.t2;go.textContent=d.go;
ok.textContent=d.ok;lang.textContent=d.lang;}
var s='ar';try{s=localStorage.getItem('erl')||'ar'}catch(e){}setLang(s);
async function init(){var b=document.getElementById('go');b.disabled=true;b.textContent=D[L].busy;
try{await fetch('/api/init',{method:'POST'});}catch(e){}
document.getElementById('ok').style.display='block';setTimeout(function(){location.href='/';},900);}
</script></body></html>
)SETUP";

/* ----------------------------- USER SETTINGS ----------------------------- */
#define IR_TX_PIN   D2        // GPIO4  -> transistor base
#define IR_RX_PIN   D5        // GPIO14 -> VS1838B OUT

const char*  AP_SSID      = "ERemote";     // open network, no password
const uint8_t AP_CHANNEL  = 6;

// ESP8266 has ONE radio => ONE TX power for both AP and STA (can't split them).
// 0..20.5 dBm. Lower = shorter range for BOTH. 17 is a balanced default;
// drop toward 10 if you want a weaker AP, but STA range to your router drops too.
const float  WIFI_TX_POWER = 17.0;

// AC readiness: some units ignore IR for a few seconds after mains power.
const uint16_t PRE_SEND_DELAY_MS = 3000;   // wait before EVERY transmit
const uint32_t BOOT_GRACE_MS     = 8000;   // no *scheduled* send this soon after boot

// Keep the AP alive this long after the router link comes up, then drop it
// (STA stays; reach the portal via the router IP). AP returns if the link is lost.
const uint32_t AP_HOLD_AFTER_STA = 180000UL;   // 3 min
const bool     MANAGE_AP         = true;

const uint16_t RECORD_TIMEOUT_MS = 15000;
/* ------------------------------------------------------------------------- */

// IR
const uint16_t kCaptureBufferSize = 1024;   // AC frames are long
const uint8_t  kTimeout           = 50;     // ms of gap = end of frame
IRrecv irrecv(IR_RX_PIN, kCaptureBufferSize, kTimeout, true);
IRsend irsend(IR_TX_PIN);
decode_results results;

// Web
ESP8266WebServer server(80);
DNSServer dnsServer;
const byte DNS_PORT = 53;

// Config held in RAM
struct Config { String ssid, pass, tz = "Asia/Baghdad"; bool ntp = true; } cfg;

// Runtime state
bool     provisioned   = false;
bool     timeValid     = false;
bool     apOn          = true;
uint32_t bootMillis    = 0;
uint32_t staConnectedAt= 0;
int      lastSchedKey  = -1;

String   recordTarget  = "";     // "on"/"off"/"eco" while capturing
uint32_t recordDeadline= 0;
String   pendingSend   = "";     // queued transmit (non-blocking delay)
uint32_t sendAt        = 0;

// Double-reset detector (RTC memory survives reset, not power loss)
const uint32_t DRD_MAGIC   = 0xE12E0007;
const uint32_t DRD_TIMEOUT = 5000;
bool     drdCleared    = false;

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
  File f=LittleFS.open("/config.json","w"); if(f){ serializeJson(d,f); f.close(); }
}
void loadConfig(){
  File f=LittleFS.open("/config.json","r"); if(!f) return;
  JsonDocument d; if(deserializeJson(d,f)==DeserializationError::Ok){
    cfg.ssid=d["ssid"]|""; cfg.pass=d["pass"]|""; cfg.tz=d["tz"]|"Asia/Baghdad"; cfg.ntp=d["ntp"]|true;
    provisioned=true;
  }
  f.close();
}

/* ------------------------------- IR store ------------------------------- */
void saveIR(const String& b, const uint16_t* raw, uint16_t len){
  JsonDocument d; d["freq"]=38; JsonArray a=d["raw"].to<JsonArray>();
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
  IPAddress apIP(192,168,4,1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255,255,255,0));
  WiFi.softAP(AP_SSID, nullptr, AP_CHANNEL);   // open network
  dnsServer.setTTL(0);                          // don't let phones cache answers
  dnsServer.start(DNS_PORT, "*", apIP);         // every hostname -> us
  apOn=true;
}
void connectSTA(){
  if(cfg.ssid.length()){ WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str()); staConnectedAt=0; }
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
  server.send_P(200, "text/html", provisioned ? PORTAL_HTML : SETUP_HTML);
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
  pendingSend=b; sendAt=millis()+PRE_SEND_DELAY_MS;   // queued; loop() fires it
  sendJson(200, "{\"ok\":true,\"delay\":"+String(PRE_SEND_DELAY_MS)+"}");
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
      uint16_t* raw=resultToRawArray(&results);
      if(raw){ saveIR(recordTarget,raw,len); delete[] raw; }
      recordTarget="";
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
    if(LittleFS.exists(irPath(b))){ pendingSend=b; sendAt=millis()+PRE_SEND_DELAY_MS; }
  }
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

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.hostname("ERemote");
  WiFi.setOutputPower(WIFI_TX_POWER);   // single radio: applies to AP and STA
  startAP();                 // also starts the captive-portal DNS hijack
  connectSTA();

  irsend.begin();
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
  server.on("/api/record",    HTTP_POST,   handleRecord);
  server.on("/api/send",      HTTP_POST,   handleSend);
  server.on("/api/wifi",      HTTP_POST,   handleWifiSave);
  server.on("/api/wifi",      HTTP_DELETE, handleWifiForget);
  server.on("/api/time",      HTTP_POST,   handleTime);
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
  clearDRD();
}
