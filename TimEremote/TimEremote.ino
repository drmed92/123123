/* ============================================================================
   TimEremote  --  ultra-low-power SCHEDULED IR remote for split ACs
   A fork of ERemote, stripped to a battery-friendly scheduler. No home Wi-Fi,
   no cloud, no genset. It records the AC remote's buttons, holds a weekly
   schedule, and spends almost all its time in deep sleep, waking only to fire
   a scheduled IR command.

   Target : ESP-12E / ESP-12F (ESP8266)   |  Arduino IDE / arduino-cli
   FQBN   : esp8266:esp8266:generic  (or d1_mini for a Wemos board)
   Libs   : ArduinoJson (v7), IRremoteESP8266

   *** REQUIRED WIRING FOR DEEP-SLEEP WAKE ***
     GPIO16 (D0) --- RST      (the on-board RST *button* does NOT do this;
                               the timer wake pulses RST through this wire)
     IR LED  -> GPIO4  via 2N2222 (GPIO4 -> ~220R -> base, emitter->GND,
                collector -> LED cathode, LED anode -> +3V3 through ~100R)
     VS1838B -> OUT to GPIO14, VCC 3V3, GND
   Without the GPIO16->RST wire the device will sleep and never wake.

   How it works:
     - Power-on or pressing RST  -> "PROGRAM MODE": brings up the AP
       "ERemoteXX" (pw 88888888) at http://4.4.4.4 for AP_WINDOW_MS so you can
       set the time, record ON/OFF/ECO, and edit the schedule. Then it sleeps.
     - Deep-sleep timer wake      -> "RUN MODE": Wi-Fi stays OFF; it sends any
       command due this minute and goes straight back to sleep.
     - To change anything, press RST to get the AP back for a few minutes.

   Clock: set by phone during the AP window; kept across sleeps in RTC memory.
   There is no internet time source, so it drifts a few minutes per day.
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

const char*  AP_SSID_BASE = "ERemote";     // + 2 per-device chars
const char*  AP_PASS      = "88888888";
const uint8_t AP_CHANNEL  = 6;

const uint32_t AP_WINDOW_MS      = 300000;  // 5 min programming window per boot
const uint16_t RECORD_TIMEOUT_MS = 30000;
const uint32_t ECO_ON_GAP_MS     = 1500;    // ON -> ECO gap when ecoNeedsOn

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

String apSsid = "ERemote";

// Config (LittleFS /cfg.json)
struct Config { bool ecoNeedsOn = false; bool ledEnabled = true; } cfg;

// Status-LED runtime
bool     ledActive = false;
uint32_t ledT0     = 0;

// Clock + fire guard kept in RTC memory (survives deep sleep, not power loss)
struct RtcState { uint32_t magic; uint32_t epoch; int16_t lastFiredMow; };
const uint32_t RTC_MAGIC = 0x54494D31;   // "TIM1"
RtcState rtc;

bool     programMode  = true;
uint32_t bootMillis   = 0;
uint32_t baseEpoch    = 0;    // nowEpoch = baseEpoch + (millis()-baseMillis)/1000
uint32_t baseMillis   = 0;

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
  JsonDocument d; if(!deserializeJson(d,f)){ cfg.ecoNeedsOn=d["ecoOn"]|false; cfg.ledEnabled=d["ledOn"]|true; }
  f.close();
}
void saveCfg(){
  JsonDocument d; d["ecoOn"]=cfg.ecoNeedsOn; d["ledOn"]=cfg.ledEnabled;
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

/* ----------------------------- sleep engine ----------------------------- */
// Minutes until the next scheduled command (1..10080; a lone schedule that is
// due right now yields ~a full week, so we don't immediately re-fire it).
int minutesToNext(int now_mow){
  JsonDocument arr; readSched(arr);
  int best=10080;
  for(JsonObject s: arr.as<JsonArray>()){
    int smow=mowOf((int)s["days"][0]|0,(int)(s["hour"]|0),(int)(s["min"]|0));
    // schedule may repeat on several weekdays:
    for(JsonVariant v: s["days"].as<JsonArray>()){
      int m=mowOf((int)v,(int)(s["hour"]|0),(int)(s["min"]|0));
      int d=((m-now_mow-1+10080)%10080)+1;             // 1..10080
      if(d<best) best=d;
    }
    (void)smow;
  }
  return best;
}
// Fire every schedule whose minute-of-week == now (once per minute).
void fireDue(int now_mow){
  if(rtc.lastFiredMow==now_mow) return;               // already fired this minute
  JsonDocument arr; readSched(arr); bool fired=false;
  for(JsonObject s: arr.as<JsonArray>()){
    for(JsonVariant v: s["days"].as<JsonArray>()){
      if(mowOf((int)v,(int)(s["hour"]|0),(int)(s["min"]|0))==now_mow){
        String act=(const char*)(s["action"]|"on");
        if(validBtn(act) && LittleFS.exists(irPath(act))){ sendAction(act); fired=true; }
      }
    }
  }
  if(fired){ rtc.lastFiredMow=now_mow; }
}
void goToSleep(){
  uint32_t now=nowEpoch();
  time_t t=(time_t)now; struct tm* g=gmtime(&t);
  int now_mow=mowOf(g->tm_wday,g->tm_hour,g->tm_min);
  int mins=minutesToNext(now_mow);                    // 1..10080
  uint32_t secs=(uint32_t)mins*60 - g->tm_sec;        // align to the target minute
  uint32_t maxS=(uint32_t)(ESP.deepSleepMax()/1000000ULL);
  if(secs>maxS) secs=maxS;                            // chunk long waits
  if(secs<1) secs=1;
  rtc.magic=RTC_MAGIC; rtc.epoch=now+secs;            // predicted wake time
  ESP.rtcUserMemoryWrite(0,(uint32_t*)&rtc,sizeof(rtc));
  WiFi.mode(WIFI_OFF);
  ESP.deepSleep((uint64_t)secs*1000000ULL, WAKE_RF_DISABLED);
}

/* ============================ HTTP handlers ============================== */
void sendJson(int code,const String& b){ server.send(code,"application/json",b); }
bool bodyJson(JsonDocument& d){ return server.hasArg("plain") && !deserializeJson(d,server.arg("plain")); }

void handleStatus(){
  JsonDocument d;
  d["codes"]["on"]  = LittleFS.exists(irPath("on"));
  d["codes"]["off"] = LittleFS.exists(irPath("off"));
  d["codes"]["eco"] = LittleFS.exists(irPath("eco"));
  d["epoch"]=nowEpoch();
  d["ecoOn"]=cfg.ecoNeedsOn;
  d["ledOn"]=cfg.ledEnabled;
  d["apLeft"]=(int)((AP_WINDOW_MS-(millis()-bootMillis))/1000);
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
void handleTime(){
  JsonDocument d; if(!bodyJson(d)){ sendJson(400,"{\"ok\":false}"); return; }
  const char* iso=d["iso"]|"";
  int y,mo,dd,h,mi;
  if(sscanf(iso,"%d-%d-%dT%d:%d",&y,&mo,&dd,&h,&mi)>=5){
    struct tm tm={}; tm.tm_year=y-1900; tm.tm_mon=mo-1; tm.tm_mday=dd; tm.tm_hour=h;
    tm.tm_min=mi; tm.tm_isdst=0;
    // TZ is fixed to UTC0 in setup(), so mktime treats these as our wall clock
    time_t t=mktime(&tm);
    baseEpoch=(uint32_t)t; baseMillis=millis();
    rtc.magic=RTC_MAGIC; rtc.epoch=baseEpoch; rtc.lastFiredMow=-1;
    ESP.rtcUserMemoryWrite(0,(uint32_t*)&rtc,sizeof(rtc));
  }
  sendJson(200,"{\"ok\":true}");
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

#include "timremote_portal.h"   // SETUP_HTML[]
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

/* --------------------------------- setup -------------------------------- */
void startProgramAP(){
  IPAddress ip(4,4,4,4);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(ip,ip,IPAddress(255,255,255,0));
  WiFi.softAP(apSsid.c_str(), AP_PASS, AP_CHANNEL);
  dnsServer.setTTL(0); dnsServer.start(DNS_PORT,"*",ip);
  server.on("/",handleRoot);
  server.on("/api/status",   HTTP_GET,    handleStatus);
  server.on("/api/record",   HTTP_POST,   handleRecord);
  server.on("/api/send",     HTTP_POST,   handleSend);
  server.on("/api/time",     HTTP_POST,   handleTime);
  server.on("/api/cfg",      HTTP_POST,   handleCfg);
  server.on("/api/schedule", HTTP_GET,    handleSchedGet);
  server.on("/api/schedule", HTTP_POST,   handleSchedPost);
  server.on("/api/schedule", HTTP_DELETE, handleSchedDel);
  server.on("/api/sleep",    HTTP_POST,   handleSleep);
  server.onNotFound(handleNotFound);
  server.begin();
}

void setup(){
  bootMillis=millis();
  Serial.begin(115200); Serial.println(F("\nTimEremote boot"));

  setenv("TZ","UTC0",1); tzset();   // no timezone math; epoch == wall clock
  if(!LittleFS.begin()){ LittleFS.format(); LittleFS.begin(); }
  loadCfg();
  irsend.begin();
  ledInit();

  { char sx[8]; snprintf(sx,sizeof(sx),"%02X",ESP.getChipId()&0xFF);
    apSsid=String(AP_SSID_BASE)+sx; }

  // Restore clock from RTC memory (valid across deep sleep, lost on power cut)
  ESP.rtcUserMemoryRead(0,(uint32_t*)&rtc,sizeof(rtc));
  if(rtc.magic!=RTC_MAGIC){ rtc.magic=RTC_MAGIC; rtc.epoch=0; rtc.lastFiredMow=-1; }
  baseEpoch=rtc.epoch; baseMillis=millis();

  bool wokeFromDeepSleep = (ESP.getResetInfoPtr()->reason == REASON_DEEP_SLEEP_AWAKE);

  if(wokeFromDeepSleep && baseEpoch>0){
    // RUN MODE: Wi-Fi stays off. Fire anything due this minute, then resleep.
    programMode=false;
    time_t t=(time_t)nowEpoch(); struct tm* g=gmtime(&t);
    fireDue(mowOf(g->tm_wday,g->tm_hour,g->tm_min));
    while(ledActive){ ledTask(); delay(4); }           // finish the fade if any
    goToSleep();                                       // never returns
  }

  // PROGRAM MODE: AP up for the window so the user can set time/record/schedule
  programMode=true;
  irrecv.setTolerance(kTolerancePct);
  irrecv.enableIRIn();
  startProgramAP();
  Serial.printf("AP %s at http://4.4.4.4  (%lus)\n", apSsid.c_str(), AP_WINDOW_MS/1000);
}

void loop(){
  if(!programMode) return;                             // (already sleeping)
  dnsServer.processNextRequest();
  server.handleClient();
  captureIR();
  ledTask();
  if(sleepRequested || millis()-bootMillis>AP_WINDOW_MS){
    delay(150); goToSleep();                            // never returns
  }
}
