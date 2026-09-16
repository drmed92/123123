/* ============================================================================
   NtpEremote  --  low-power NTP-SCHEDULED IR remote for split ACs
   A fork of ERemote/TimEremote. Instead of a hand-set clock that drifts, it
   joins the home Wi-Fi just long enough to get the real time from NTP
   (Baghdad, UTC+3), then spends almost all its life in deep sleep, waking
   only to fire a scheduled IR command -- and re-checking NTP shortly before
   each event so the shot lands on the right minute.

   Target : ESP-12E / ESP-12F / NodeMCU v3 (ESP8266)  | Arduino IDE / arduino-cli
   FQBN   : esp8266:esp8266:nodemcuv2  (or :generic for a bare ESP-12)
   Libs   : ArduinoJson (v7), IRremoteESP8266

   *** REQUIRED WIRING FOR DEEP-SLEEP WAKE ***
     GPIO16 (D0) --- RST      (the on-board RST *button* does NOT do this;
                               the timer wake pulses RST through this wire)
     IR LED  -> GPIO4  via 2N2222 (GPIO4 -> ~220R -> base, emitter->GND,
                collector -> LED cathode, LED anode -> +3V3 through ~100R)
     VS1838B -> OUT to GPIO14, VCC 3V3, GND
   Without the GPIO16->RST wire the device will sleep and never wake.

   *** NodeMCU v3 (LoLin) NOTE ***
     The AMS1117 regulator + CP2102 USB chip draw ~1-2 mA even in deep sleep,
     no matter how good this code is. For true uA-level battery life use a
     bare ESP-12 (or remove those parts). The firmware is identical.

   Life cycle:
     - First power-on (no Wi-Fi saved) or pressing RST -> "SETUP": brings up
       the AP "NtEremoteXX" (pw 88888888) at http://4.4.4.4 for AP_WINDOW_MS
       (3 min) so you can enter the home Wi-Fi, record ON/OFF/ECO, and edit
       the weekly schedule. Then it NTP-syncs and sleeps.
     - Power-on WITH Wi-Fi saved (e.g. after a battery change) -> silent
       recovery: no AP, just STA + NTP, then straight into the schedule.
     - Deep-sleep timer wake -> "RUN": Wi-Fi stays off until we are close to
       an event; then it re-syncs NTP, holds on the accurate crystal, fires
       exactly on the scheduled minute, and sleeps to the next one.

   Clock: NTP over home Wi-Fi. Between syncs the internal RC oscillator keeps
   time in RTC memory but drifts ~1-3%/hr of sleep, so far-out sleeps wake
   ~20% early (DRIFT_MARGIN) to never overshoot; a fresh NTP sync right before
   each event erases the accumulated error.
   ============================================================================ */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>
#include <time.h>

/* ----------------------------- USER SETTINGS ----------------------------- */
#define IR_TX_PIN   D2        // GPIO4  -> transistor base   (bare ESP-12: 4)
#define IR_RX_PIN   D5        // GPIO14 -> VS1838B OUT       (bare ESP-12: 14)

const char*  AP_SSID_BASE = "NtEremote";   // + 2 per-device chars
const char*  AP_PASS      = "88888888";
const uint8_t AP_CHANNEL  = 6;

const uint32_t AP_WINDOW_MS      = 180000;  // 3 min programming window per boot
const uint16_t RECORD_TIMEOUT_MS = 30000;
const uint32_t ECO_ON_GAP_MS     = 1500;    // ON -> ECO gap when ecoNeedsOn

// --- Time / NTP ---
const long     TZ_OFFSET_S   = 3 * 3600;    // Baghdad = UTC+3 (no DST)
const uint32_t STA_CONN_MS   = 12000;       // give up joining Wi-Fi after this
const uint32_t NTP_SYNC_MS   = 8000;        // give up on NTP reply after this
const uint32_t MIN_VALID_EP  = 1700000000;  // ~2023-11; anything less = not synced

// --- Sleep / wake scheduling ---
const float    DRIFT_MARGIN  = 0.20f;       // dead-reckoned sleeps wake 20% early
const uint32_t NTP_LEAD_S    = 1500;        // within 25 min of an event -> re-sync
const uint32_t HOLD_S        = 120;         // within 2 min -> stay awake & fire
const uint32_t SLEEP_MIN_S   = 2;           // never deep-sleep for < this

// ---- Status LED: a smooth heartbeat fade after every IR send ----
#define LED_PIN            LED_BUILTIN     // GPIO2 on NodeMCU/ESP-12E (active-low)
const bool     LED_ACTIVE_LOW = true;
const uint16_t LED_PULSE_MS   = 1250;      // duration of one fade in+out
const uint8_t  LED_PULSES     = 1;         // heartbeats per IR send
const uint16_t LED_MAX        = 1023;      // PWM range / peak brightness (0..1023)
/* ------------------------------------------------------------------------- */

const uint16_t kCaptureBufferSize = 1536;
const uint8_t  kTimeout           = 50;
const uint8_t  kTolerancePct      = 35;
const uint16_t kMinFrameLen       = 12;
IRrecv irrecv(IR_RX_PIN, kCaptureBufferSize, kTimeout, true);
IRsend irsend(IR_TX_PIN);
decode_results results;

ESP8266WebServer server(80);
DNSServer dnsServer;
const byte DNS_PORT = 53;

String apSsid = "NtEremote";

// Config (LittleFS /cfg.json)
struct Config {
  String staSsid = "";
  String staPass = "";
  bool   ecoNeedsOn = false;
  bool   ledEnabled = true;
} cfg;

// Status-LED runtime
bool     ledActive = false;
uint32_t ledT0     = 0;

// Clock + fire guard kept in RTC memory (survives deep sleep, not power loss).
// drSince = dead-reckoned (un-NTP'd) sleep chunks since the last sync; capped so
// drift can never compound across two blind sleeps and overshoot an event.
struct RtcState { uint32_t magic; uint32_t epoch; int16_t lastFiredMow; uint16_t drSince; };
const uint32_t RTC_MAGIC = 0x4E545032;   // "NTP2"
const uint16_t MAX_DR_CHUNKS = 1;        // force an NTP re-sync after this many
RtcState rtc;

bool     setupMode    = true;   // AP + web portal up (programming window)
uint32_t bootMillis   = 0;
uint32_t baseEpoch    = 0;      // nowEpoch = baseEpoch + (millis()-baseMillis)/1000
uint32_t baseMillis   = 0;
bool     haveTime     = false;  // true once we have a trustworthy clock this boot

// Setup-window live state (for the portal)
String   lastNtpResult = "";    // "", "ok", "fail"

// Record state
String   recordTarget = "";
uint32_t recordDeadline = 0;
String   lastCapProto = ""; uint16_t lastCapLen = 0; bool lastCapOvf = false;
String   lastCapBtn = ""; uint16_t capSeq = 0;

bool     sleepRequested = false;

/* ------------------------------- helpers -------------------------------- */
bool validBtn(const String& b){ return b=="on"||b=="off"||b=="eco"; }
String irPath(const String& b){ return "/ir_"+b+".json"; }
uint32_t nowEpoch(){ return baseEpoch + (millis()-baseMillis)/1000; }

void loadCfg(){
  File f=LittleFS.open("/cfg.json","r"); if(!f) return;
  JsonDocument d;
  if(!deserializeJson(d,f)){
    cfg.staSsid   = (const char*)(d["ssid"]|"");
    cfg.staPass   = (const char*)(d["pass"]|"");
    cfg.ecoNeedsOn= d["ecoOn"]|false;
    cfg.ledEnabled= d["ledOn"]|true;
  }
  f.close();
}
void saveCfg(){
  JsonDocument d;
  d["ssid"]=cfg.staSsid; d["pass"]=cfg.staPass;
  d["ecoOn"]=cfg.ecoNeedsOn; d["ledOn"]=cfg.ledEnabled;
  File f=LittleFS.open("/cfg.json","w"); if(f){ serializeJson(d,f); f.close(); }
}

/* ------------------------------- status LED ----------------------------- */
void ledWrite(uint16_t v){                          // v: 0..LED_MAX brightness
  analogWrite(LED_PIN, LED_ACTIVE_LOW ? (LED_MAX - v) : v);
}
void ledInit(){ pinMode(LED_PIN,OUTPUT); analogWriteRange(LED_MAX); ledWrite(0); }
void ledStart(){ if(cfg.ledEnabled){ ledActive=true; ledT0=millis(); } }
void ledTask(){                                     // non-blocking; call from loop
  if(!ledActive) return;
  uint32_t el=millis()-ledT0, total=(uint32_t)LED_PULSE_MS*LED_PULSES;
  if(el>=total){ ledActive=false; ledWrite(0); return; }
  float ph=(el % LED_PULSE_MS)/(float)LED_PULSE_MS;  // 0..1 within a pulse
  ledWrite((uint16_t)(sinf(ph*PI)*LED_MAX));         // smooth fade in then out
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
// Fire a scheduled/manual action, honouring the ECO-needs-power-on chain.
void sendAction(const String& b){
  if(b=="eco" && cfg.ecoNeedsOn && LittleFS.exists(irPath("on"))){
    sendIR("on"); delay(ECO_ON_GAP_MS); sendIR("eco");
  } else sendIR(b);
  ledStart();                                        // heartbeat "IR sent" feedback
}

/* ------------------------------ schedules ------------------------------- */
void readSched(JsonDocument& d){
  File f=LittleFS.open("/sched.json","r");
  if(!f || deserializeJson(d,f)){ d.to<JsonArray>(); }
  if(f) f.close();
  if(!d.is<JsonArray>()) d.to<JsonArray>();
}
void writeSched(JsonDocument& d){
  File f=LittleFS.open("/sched.json","w"); if(f){ serializeJson(d,f); f.close(); }
}
int mowOf(int wday,int hour,int min){ return wday*1440 + hour*60 + min; }  // minute-of-week

// Minutes until the next scheduled command (1..10080; a lone schedule that is
// due right now yields ~a full week, so we don't immediately re-fire it).
// Returns 10080 and sets *any=false when there are no schedules at all.
int minutesToNext(int now_mow, bool* any){
  JsonDocument arr; readSched(arr);
  int best=10080; bool found=false;
  for(JsonObject s: arr.as<JsonArray>()){
    for(JsonVariant v: s["days"].as<JsonArray>()){
      found=true;
      int m=mowOf((int)v,(int)(s["hour"]|0),(int)(s["min"]|0));
      int d=((m-now_mow-1+10080)%10080)+1;             // 1..10080
      if(d<best) best=d;
    }
  }
  if(any) *any=found;
  return best;
}

// Absolute wall-clock epoch of the next scheduled minute (its :00 second) at or
// after 'fromWall'. When no schedules exist, returns fromWall + ~1 week.
uint32_t nextEventEpoch(uint32_t fromWall){
  time_t t=(time_t)fromWall; struct tm* g=gmtime(&t);
  int now_mow=mowOf(g->tm_wday,g->tm_hour,g->tm_min);
  bool any=false;
  int mins=minutesToNext(now_mow,&any);                // 1..10080
  uint32_t secs=(uint32_t)mins*60 - g->tm_sec;         // align to target's :00
  return fromWall + secs;
}

// Fire every schedule whose minute-of-week == mow (once per minute).
void fireDue(int mow){
  if(rtc.lastFiredMow==mow) return;                    // already fired this minute
  JsonDocument arr; readSched(arr); bool fired=false;
  for(JsonObject s: arr.as<JsonArray>()){
    for(JsonVariant v: s["days"].as<JsonArray>()){
      if(mowOf((int)v,(int)(s["hour"]|0),(int)(s["min"]|0))==mow){
        String act=(const char*)(s["action"]|"on");
        if(validBtn(act) && LittleFS.exists(irPath(act))){ sendAction(act); fired=true; }
      }
    }
  }
  if(fired){ rtc.lastFiredMow=mow; }
}

/* ------------------------------ Wi-Fi / NTP ----------------------------- */
// Join the saved home Wi-Fi. Returns true once associated (STA mode left on).
bool staConnect(uint32_t timeoutMs){
  if(cfg.staSsid.length()==0) return false;
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.staSsid.c_str(), cfg.staPass.c_str());
  uint32_t t0=millis();
  while(WiFi.status()!=WL_CONNECTED && millis()-t0<timeoutMs){ delay(150); yield(); }
  return WiFi.status()==WL_CONNECTED;
}
// Ask NTP for the time; on success set our wall clock (baseEpoch = Baghdad epoch).
bool ntpSync(uint32_t timeoutMs){
  configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  uint32_t t0=millis();
  while(millis()-t0<timeoutMs){
    time_t now=time(nullptr);
    if((uint32_t)now > MIN_VALID_EP){
      baseEpoch=(uint32_t)now + TZ_OFFSET_S;           // store wall-clock epoch
      baseMillis=millis();
      haveTime=true;
      return true;
    }
    delay(120); yield();
  }
  return false;
}
// Connect + sync in one shot, then drop the radio. Updates lastNtpResult.
bool refreshTime(){
  bool ok = staConnect(STA_CONN_MS) && ntpSync(NTP_SYNC_MS);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  lastNtpResult = ok ? "ok" : "fail";
  return ok;
}

/* ----------------------------- sleep engine ----------------------------- */
void deepSleepSecs(uint32_t secs){
  if(secs<SLEEP_MIN_S) secs=SLEEP_MIN_S;
  uint32_t maxS=(uint32_t)(ESP.deepSleepMax()/1000000ULL);
  if(secs>maxS) secs=maxS;                             // chunk long waits
  rtc.magic=RTC_MAGIC; rtc.epoch=nowEpoch()+secs;      // predicted wake time
  ESP.rtcUserMemoryWrite(0,(uint32_t*)&rtc,sizeof(rtc));
  WiFi.mode(WIFI_OFF);
  ESP.deepSleep((uint64_t)secs*1000000ULL, WAKE_RF_DISABLED);
}
// Dead-reckoned sleep toward a far target: wake DRIFT_MARGIN early so a slow
// RC clock still arrives before the event (where NTP then corrects it).
void sleepTowardEarly(uint32_t targetWall){
  uint32_t now=nowEpoch();
  int32_t dt=(int32_t)(targetWall-now);
  if(dt<(int32_t)SLEEP_MIN_S){ deepSleepSecs(SLEEP_MIN_S); return; }
  deepSleepSecs((uint32_t)(dt*(1.0f-DRIFT_MARGIN)));
}
// Accurate short sleep toward a near target (clock just NTP-synced): no margin.
void sleepTowardExact(uint32_t targetWall){
  uint32_t now=nowEpoch();
  int32_t dt=(int32_t)(targetWall-now);
  deepSleepSecs(dt<(int32_t)SLEEP_MIN_S ? SLEEP_MIN_S : (uint32_t)dt);
}

// One pass of the RUN-mode state machine. Always ends by deep-sleeping.
void runOnce(){
  uint32_t est=nowEpoch();
  uint32_t next=nextEventEpoch(est);
  int32_t  dt=(int32_t)(next-est);

  // Re-sync NTP when we are close to an event OR when we have already dead-
  // reckoned a full chunk since the last sync (so blind drift never compounds
  // across two sleeps and overshoots — matters for gaps longer than one sleep).
  bool mustSync = (dt <= (int32_t)NTP_LEAD_S) || (rtc.drSince >= MAX_DR_CHUNKS);
  if(mustSync){
    refreshTime();
    rtc.drSince=0;
    est=nowEpoch();
    // Safety net: if drift landed us a few seconds INTO the target minute, fire
    // it now rather than skip a whole week (lastFiredMow guards repeats).
    { time_t tn=(time_t)est; struct tm* gn=gmtime(&tn);
      fireDue(mowOf(gn->tm_wday,gn->tm_hour,gn->tm_min)); }
    next=nextEventEpoch(est); dt=(int32_t)(next-est);
  }

  if(dt > (int32_t)NTP_LEAD_S){
    // Still far (either no sync was needed, or we just did a mid-flight sync):
    // dead-reckon ONE margined chunk from the freshest clock we have.
    rtc.drSince++;                                     // persisted by deepSleepSecs
    sleepTowardEarly(next);                            // never returns
  }

  if(dt > (int32_t)HOLD_S){
    // Close and just synced: short, accurate sleep to just inside the hold
    // window. Next wake re-syncs once more, then holds & fires.
    sleepTowardExact(next - HOLD_S + 5);               // never returns
  }

  // Final approach: stay awake on the accurate 26 MHz crystal (radio off) and
  // fire exactly when the scheduled minute arrives.
  while((int32_t)(next - nowEpoch()) > 0){ ledTask(); yield(); delay(20); }
  time_t t=(time_t)next; struct tm* g=gmtime(&t);
  fireDue(mowOf(g->tm_wday,g->tm_hour,g->tm_min));
  while(ledActive){ ledTask(); delay(4); }             // let the heartbeat finish

  // Sleep toward the following event (skip the minute we just fired).
  rtc.drSince=0;
  uint32_t after=nextEventEpoch(nowEpoch()+60);
  sleepTowardEarly(after);                             // never returns
}

/* ============================ HTTP handlers ============================== */
void restartAP();            // defined in the AP/portal section; used by handleWifi()
void sendJson(int code,const String& b){ server.send(code,"application/json",b); }
bool bodyJson(JsonDocument& d){ return server.hasArg("plain") && !deserializeJson(d,server.arg("plain")); }

void handleStatus(){
  JsonDocument d;
  d["codes"]["on"]  = LittleFS.exists(irPath("on"));
  d["codes"]["off"] = LittleFS.exists(irPath("off"));
  d["codes"]["eco"] = LittleFS.exists(irPath("eco"));
  d["epoch"]  = haveTime ? nowEpoch() : 0;
  d["haveTime"]= haveTime;
  d["ssid"]   = cfg.staSsid;
  d["hasPass"]= cfg.staPass.length()>0;
  d["ntp"]    = lastNtpResult;
  d["ecoOn"]  = cfg.ecoNeedsOn;
  d["ledOn"]  = cfg.ledEnabled;
  d["apLeft"] = (int)((AP_WINDOW_MS-(millis()-bootMillis))/1000);
  d["lastCapture"]["btn"]=lastCapBtn;
  d["lastCapture"]["proto"]=lastCapProto;
  d["lastCapture"]["seq"]=capSeq;
  d["lastCapture"]["overflow"]=lastCapOvf;
  JsonDocument sd; readSched(sd); d["schedules"]=sd.as<JsonArray>();
  String out; serializeJson(d,out); sendJson(200,out);
}
void handleRecord(){
  String b=server.arg("btn");
  if(!validBtn(b)){ sendJson(400,"{\"ok\":false}"); return; }
  recordTarget=b; recordDeadline=millis()+RECORD_TIMEOUT_MS; irrecv.resume();
  sendJson(200,"{\"ok\":true}");
}
void handleSend(){
  String b=server.arg("btn");
  if(!validBtn(b) || !LittleFS.exists(irPath(b))){ sendJson(400,"{\"ok\":false}"); return; }
  sendAction(b); sendJson(200,"{\"ok\":true}");
}
// Save home Wi-Fi credentials, then try an NTP sync so the user sees it work.
void handleWifi(){
  JsonDocument d; if(!bodyJson(d)){ sendJson(400,"{\"ok\":false}"); return; }
  cfg.staSsid=(const char*)(d["ssid"]|"");
  if(d["pass"].is<const char*>()) cfg.staPass=(const char*)(d["pass"]|"");
  saveCfg();
  // Re-enable the AP after the STA test so the portal stays reachable.
  bool ok=false;
  if(cfg.staSsid.length()){ ok = staConnect(STA_CONN_MS) && ntpSync(NTP_SYNC_MS); }
  WiFi.disconnect(true);
  restartAP();
  lastNtpResult = cfg.staSsid.length() ? (ok?"ok":"fail") : "";
  sendJson(200, String("{\"ok\":true,\"ntp\":")+(ok?"true":"false")+"}");
}
void handleCfg(){
  JsonDocument d; if(!bodyJson(d)){ sendJson(400,"{\"ok\":false}"); return; }
  if(d["ecoOn"].is<bool>()) cfg.ecoNeedsOn=d["ecoOn"];
  if(d["ledOn"].is<bool>()) cfg.ledEnabled=d["ledOn"];
  saveCfg();
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
  uint32_t id=in["id"].is<uint32_t>()?(uint32_t)in["id"]:(uint32_t)millis();
  JsonObject o=a.add<JsonObject>();
  o["id"]=id; o["action"]=(const char*)(in["action"]|"on");
  o["hour"]=(int)(in["hour"]|0); o["min"]=(int)(in["min"]|0);
  JsonArray days=o["days"].to<JsonArray>();
  for(JsonVariant v:in["days"].as<JsonArray>()) days.add((int)v);
  writeSched(arr); sendJson(200,"{\"ok\":true,\"id\":"+String(id)+"}");
}
void handleSchedDel(){
  if(!server.hasArg("id")){ sendJson(400,"{\"ok\":false}"); return; }
  uint32_t id=strtoul(server.arg("id").c_str(),nullptr,10);
  JsonDocument arr; readSched(arr); JsonArray a=arr.as<JsonArray>();
  for(size_t i=0;i<a.size();i++){ if((uint32_t)a[i]["id"]==id){ a.remove(i); break; } }
  writeSched(arr); sendJson(200,"{\"ok\":true}");
}
void handleSleep(){ sendJson(200,"{\"ok\":true}"); sleepRequested=true; }
void handleNotFound(){
  server.sendHeader("Location", String("http://")+WiFi.softAPIP().toString(), true);
  server.send(302,"text/plain","");
}

#include "ntp_portal.h"   // SETUP_HTML[]
void handleRoot(){ server.send_P(200,"text/html",SETUP_HTML); }

/* ------------------------------- capture -------------------------------- */
void captureIR(){
  if(irrecv.decode(&results)){
    if(recordTarget!=""){
      uint16_t len=getCorrectedRawLength(&results);
      if(len>=kMinFrameLen){
        uint16_t* raw=resultToRawArray(&results);
        if(raw){
          String proto=typeToString(results.decode_type);
          saveIR(recordTarget,raw,len,proto); delete[] raw;
          lastCapBtn=recordTarget; lastCapProto=proto; lastCapLen=len;
          lastCapOvf=results.overflow; capSeq++; recordTarget="";
        }
      }
    }
    irrecv.resume();
  }
  if(recordTarget!="" && (int32_t)(millis()-recordDeadline)>0) recordTarget="";
}

/* ------------------------------ AP / portal ----------------------------- */
void routes(){
  server.on("/",handleRoot);
  server.on("/api/status",   HTTP_GET,    handleStatus);
  server.on("/api/record",   HTTP_POST,   handleRecord);
  server.on("/api/send",     HTTP_POST,   handleSend);
  server.on("/api/wifi",     HTTP_POST,   handleWifi);
  server.on("/api/cfg",      HTTP_POST,   handleCfg);
  server.on("/api/schedule", HTTP_GET,    handleSchedGet);
  server.on("/api/schedule", HTTP_POST,   handleSchedPost);
  server.on("/api/schedule", HTTP_DELETE, handleSchedDel);
  server.on("/api/sleep",    HTTP_POST,   handleSleep);
  server.onNotFound(handleNotFound);
}
void restartAP(){
  IPAddress ip(4,4,4,4);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(ip,ip,IPAddress(255,255,255,0));
  WiFi.softAP(apSsid.c_str(), AP_PASS, AP_CHANNEL);
}
void startSetupAP(){
  restartAP();
  dnsServer.setTTL(0); dnsServer.start(DNS_PORT,"*",IPAddress(4,4,4,4));
  routes();
  server.begin();
}

/* --------------------------------- setup -------------------------------- */
void setup(){
  bootMillis=millis();
  Serial.begin(115200); Serial.println(F("\nNtpEremote boot"));

  setenv("TZ","UTC0",1); tzset();   // we store Baghdad wall-clock epoch directly
  if(!LittleFS.begin()){ LittleFS.format(); LittleFS.begin(); }
  loadCfg();
  irsend.begin();
  ledInit();

  { char sx[8]; snprintf(sx,sizeof(sx),"%02X",ESP.getChipId()&0xFF);
    apSsid=String(AP_SSID_BASE)+sx; }

  // Restore clock from RTC memory (valid across deep sleep, lost on power cut)
  ESP.rtcUserMemoryRead(0,(uint32_t*)&rtc,sizeof(rtc));
  if(rtc.magic!=RTC_MAGIC){ rtc.magic=RTC_MAGIC; rtc.epoch=0; rtc.lastFiredMow=-1; rtc.drSince=0; }
  baseEpoch=rtc.epoch; baseMillis=millis(); haveTime=(baseEpoch>0);

  uint32_t reason = ESP.getResetInfoPtr()->reason;
  bool wokeDeep  = (reason == REASON_DEEP_SLEEP_AWAKE);
  bool rstButton = (reason == REASON_EXT_SYS_RST);
  bool hasCreds  = cfg.staSsid.length()>0;

  if(wokeDeep && haveTime){
    // RUN MODE: no AP. Handle this event window, then sleep again.
    setupMode=false;
    runOnce();                                         // never returns
  }

  if(!rstButton && hasCreds){
    // Power-on with saved Wi-Fi (e.g. battery change): recover silently, no AP.
    if(refreshTime()){
      setupMode=false;
      runOnce();                                       // never returns
    }
    // NTP failed -> fall through to the AP so the user can fix Wi-Fi.
  }

  // SETUP MODE: AP up for the window so the user can enter Wi-Fi / record / schedule.
  setupMode=true;
  irrecv.setTolerance(kTolerancePct);
  irrecv.enableIRIn();
  startSetupAP();
  Serial.printf("AP %s at http://4.4.4.4  (%lus)\n", apSsid.c_str(), AP_WINDOW_MS/1000);
}

void loop(){
  if(!setupMode) return;                               // (already sleeping)
  dnsServer.processNextRequest();
  server.handleClient();
  captureIR();
  ledTask();
  if(sleepRequested || millis()-bootMillis>AP_WINDOW_MS){
    delay(150);
    server.stop(); dnsServer.stop();
    // Get the real time before the long sleep so the first event lands right.
    refreshTime();
    if(!haveTime){                                     // never synced -> nothing to schedule
      deepSleepSecs(3600);                             // retry in an hour (never returns)
    }
    rtc.drSince=0;
    runOnce();                                         // enter the scheduler; never returns
  }
}
