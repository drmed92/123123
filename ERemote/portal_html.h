/* portal_html.h -- ERemote main control portal (served when provisioned).
   Bilingual English / Arabic with RTL support; language choice persists in
   localStorage (key "erl", shared with the setup wizard page).
   Talks to the /api/* endpoints defined in ERemote.ino. */
#pragma once

const char PORTAL_HTML[] PROGMEM = R"PORTAL(
<!DOCTYPE html><html lang="ar" dir="rtl"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ERemote</title><style>
:root{--b:#0f766e;--b2:#0ea5a4;--bg:#0b1220;--card:#131c2b;--line:#243244;
--txt:#e6edf5;--mut:#93a1b5;--ok:#86efac;--bad:#fca5a5;--warn:#fcd34d}
*{box-sizing:border-box}
body{margin:0;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Tahoma,sans-serif;
background:var(--bg);color:var(--txt);min-height:100vh;padding:16px 16px 40px}
.wrap{max-width:440px;margin:0 auto}
header{display:flex;align-items:center;gap:12px;margin-bottom:16px}
.logo{width:40px;height:40px;border-radius:12px;flex:none;
background:linear-gradient(135deg,var(--b),var(--b2));display:grid;place-items:center}
.logo svg{width:22px;height:22px;color:#fff}
header h1{font-size:18px;margin:0;flex:1}
#clock{font-size:12px;color:var(--mut)}
.lang{padding:6px 14px;font-size:13px;font-weight:600;background:var(--line);
border:0;border-radius:10px;color:var(--txt);cursor:pointer;font-family:inherit}
section{background:var(--card);border:1px solid var(--line);border-radius:16px;
padding:16px;margin-bottom:14px}
h2{font-size:14px;margin:0 0 12px;color:var(--mut);font-weight:600;
text-transform:uppercase;letter-spacing:.04em}
.row{display:flex;align-items:center;justify-content:space-between;gap:10px;
padding:10px 0;border-top:1px solid var(--line)}
.row:first-of-type{border-top:0}
.bt{font-weight:700;font-size:15px}
.st{font-size:12px;color:var(--mut)}
.st.on{color:var(--ok)}
.acts{display:flex;gap:8px;flex:none}
button{border:0;border-radius:12px;padding:10px 16px;font-size:14px;font-weight:700;
color:#fff;background:var(--b);cursor:pointer;font-family:inherit}
button.sec{background:var(--line)}
button.danger{background:#7f1d1d;width:100%}
button:disabled{opacity:.5}
.msg{font-size:13px;color:var(--warn);margin-top:10px;display:none}
input,select{width:100%;padding:11px;margin-bottom:10px;border:1px solid var(--line);
border-radius:10px;background:var(--bg);color:var(--txt);font-size:15px;font-family:inherit}
label{display:block;font-size:12px;color:var(--mut);margin-bottom:4px}
.inline{display:flex;gap:8px}
.inline>*{flex:1}
.days{display:flex;gap:6px;margin-bottom:12px;flex-wrap:wrap}
.day{flex:1;min-width:38px;padding:8px 0;text-align:center;font-size:12px;font-weight:600;
border:1px solid var(--line);border-radius:9px;background:var(--bg);color:var(--mut);
cursor:pointer;user-select:none}
.day.sel{background:var(--b);border-color:var(--b);color:#fff}
.sched{display:flex;align-items:center;justify-content:space-between;gap:8px;
padding:9px 0;border-bottom:1px solid var(--line);font-size:14px}
.sched .del{background:none;color:var(--bad);font-size:18px;padding:2px 10px}
.sched .meta{color:var(--mut);font-size:12px}
.empty{color:var(--mut);font-size:13px;padding:6px 0}
.kv{display:flex;justify-content:space-between;font-size:13px;padding:4px 0}
.kv span:first-child{color:var(--mut)}
.radio{display:flex;align-items:center;gap:8px;margin-bottom:10px;font-size:14px}
.radio input{width:auto;margin:0}
.desc{font-size:13px;color:var(--mut);margin:0 0 10px;line-height:1.5}
#toast{position:fixed;bottom:18px;left:50%;transform:translateX(-50%);
background:#1e293b;border:1px solid var(--line);color:var(--txt);padding:11px 18px;
border-radius:12px;font-size:14px;display:none;max-width:90vw;box-shadow:0 8px 24px rgba(0,0,0,.5)}
</style></head><body><div class="wrap">

<header>
  <div class="logo"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"
  stroke-linecap="round"><path d="M4 6h16v9H4z"/><path d="M8 19h8M12 15v4"/>
  <circle cx="8" cy="10.5" r="1"/><path d="M12 9v3M16 9v3"/></svg></div>
  <h1>ERemote <div id="clock"></div></h1>
  <button class="lang" id="lang" onclick="setLang(L=='en'?'ar':'en')">عربي</button>
</header>

<section>
  <h2 data-k="remote"></h2>
  <div class="row"><div><div class="bt" data-k="on"></div><div class="st" id="st_on"></div></div>
    <div class="acts"><button onclick="doSend('on')" data-k="send"></button>
    <button class="sec" onclick="doRec('on')" data-k="rec"></button></div></div>
  <div class="row"><div><div class="bt" data-k="off"></div><div class="st" id="st_off"></div></div>
    <div class="acts"><button onclick="doSend('off')" data-k="send"></button>
    <button class="sec" onclick="doRec('off')" data-k="rec"></button></div></div>
  <div class="row"><div><div class="bt" data-k="eco"></div><div class="st" id="st_eco"></div></div>
    <div class="acts"><button onclick="doSend('eco')" data-k="send"></button>
    <button class="sec" onclick="doRec('eco')" data-k="rec"></button></div></div>
  <div class="msg" id="recmsg"></div>
</section>

<section>
  <h2 data-k="genset"></h2>
  <p class="desc" data-k="gsDesc"></p>
  <div class="kv"><span data-k="status"></span><span id="gdet">-</span></div>
  <div style="margin-top:8px">
    <div class="radio"><input type="radio" name="gs" id="gs_dis" value="disabled" checked>
      <label for="gs_dis" style="margin:0" data-k="gsDis"></label></div>
    <div class="radio"><input type="radio" name="gs" id="gs_off" value="off">
      <label for="gs_off" style="margin:0" data-k="gsOff"></label></div>
    <div class="radio"><input type="radio" name="gs" id="gs_eco" value="eco">
      <label for="gs_eco" style="margin:0" data-k="gsEco"></label></div>
    <label data-k="gsDelay"></label>
    <select id="g_delay"></select>
    <label data-k="gsSsid"></label>
    <input id="g_ssid" maxlength="32" value="GENSET_ACTIVE">
    <button style="width:100%" onclick="gsSave()" data-k="save"></button>
  </div>
</section>

<section>
  <h2 data-k="schedules"></h2>
  <div id="slist"></div>
  <div style="margin-top:12px">
    <div class="inline">
      <div><label data-k="action"></label>
        <select id="s_act"><option value="on" data-k="on"></option>
        <option value="off" data-k="off"></option>
        <option value="eco" data-k="eco"></option></select></div>
      <div><label data-k="at"></label><input id="s_time" type="time" value="14:00"></div>
    </div>
    <label data-k="days"></label>
    <div class="days" id="s_days"></div>
    <button style="width:100%" onclick="addSched()" data-k="add"></button>
  </div>
</section>

<section>
  <h2 data-k="wifi"></h2>
  <div class="kv"><span data-k="status"></span><span id="wst"></span></div>
  <div class="kv"><span>IP</span><span id="wip">-</span></div>
  <div style="margin-top:8px">
    <label data-k="ssid"></label><input id="w_ssid" maxlength="32">
    <label data-k="pass"></label><input id="w_pass" type="password" maxlength="64">
    <div class="inline">
      <button onclick="wifiSave()" data-k="save"></button>
      <button class="sec" onclick="wifiForget()" data-k="forget"></button>
    </div>
  </div>
</section>

<section>
  <h2 data-k="time"></h2>
  <div class="kv"><span data-k="devtime"></span><span id="now">-</span></div>
  <div style="margin-top:8px">
    <div class="radio"><input type="radio" name="tm" id="tm_ntp" checked
      onchange="manualBox()"><label for="tm_ntp" style="margin:0" data-k="ntp"></label></div>
    <div class="radio"><input type="radio" name="tm" id="tm_man"
      onchange="manualBox()"><label for="tm_man" style="margin:0" data-k="manual"></label></div>
    <div id="manbox" style="display:none"><input id="t_iso" type="datetime-local"></div>
    <label data-k="tz"></label>
    <select id="t_tz">
      <option value="Asia/Baghdad" data-k="baghdad"></option>
      <option value="Asia/Dubai" data-k="dubai"></option>
    </select>
    <button style="width:100%" onclick="timeSave()" data-k="apply"></button>
  </div>
</section>

<section>
  <h2 data-k="advanced"></h2>
  <button class="danger" onclick="factory()" data-k="factory"></button>
</section>

</div><div id="toast"></div><script>
var D={
en:{remote:'Remote',on:'Turn ON',off:'Turn OFF',eco:'ECO mode',send:'Send',rec:'Record',
recorded:'Recorded',empty:'Not recorded',noCode:'Record this button first.',
recording:'Point the AC remote at the device and press its button now…',
recOk:'Code recorded!',recFail:'Nothing received — try again.',
queued:'Sending in 3 seconds — make sure the device faces the AC.',
schedules:'Schedules',action:'Action',at:'Time',days:'Days',add:'Add schedule',
none:'No schedules yet.',pickDay:'Pick at least one day.',
genset:'AutoGenset',
gsDesc:"When the generator's Wi-Fi network appears (neighborhood genset switched on), the device automatically sends a command to the AC.",
gsDis:'Disabled',gsOff:'Turn AC OFF',gsEco:'Switch to ECO',
gsDelay:'Delay before sending',gsSsid:'Generator network name',
gsDet:'Generator detected',gsNo:'Not detected',
delayS:['3 seconds','5 seconds','10 seconds','15 seconds','30 seconds','1 minute','2 minutes','5 minutes'],
wifi:'Wi-Fi',status:'Status',conn:'Connected',noconn:'Not connected',
ssid:'Network name (SSID)',pass:'Password',save:'Save',forget:'Forget network',
time:'Time & clock',devtime:'Device time',ntp:'Automatic (internet time)',
manual:'Set manually',tz:'Time zone',apply:'Apply',
baghdad:'Baghdad / Kuwait / Riyadh (GMT+3)',dubai:'Dubai (GMT+4)',
advanced:'Advanced',factory:'Factory reset',
factoryMsg:'Erase EVERYTHING (codes, Wi-Fi, schedules)?',
saved:'Saved.',err:'Error — try again.',lang:'عربي',
dayS:['Sun','Mon','Tue','Wed','Thu','Fri','Sat'],
actS:{on:'ON',off:'OFF',eco:'ECO'}},
ar:{remote:'التحكم',on:'تشغيل',off:'إطفاء',eco:'الوضع الاقتصادي',send:'إرسال',rec:'تسجيل',
recorded:'مسجَّل',empty:'غير مسجَّل',noCode:'سجِّل هذا الزر أولاً.',
recording:'وجِّه ريموت المكيف نحو الجهاز واضغط الزر الآن…',
recOk:'تم تسجيل الكود!',recFail:'لم يصل شيء — حاول مجدداً.',
queued:'سيتم الإرسال خلال ٣ ثوانٍ — تأكد أن الجهاز موجَّه نحو المكيف.',
schedules:'الجدولة',action:'الإجراء',at:'الوقت',days:'الأيام',add:'إضافة جدولة',
none:'لا توجد جدولات بعد.',pickDay:'اختر يوماً واحداً على الأقل.',
genset:'كشف المولّدة تلقائياً',
gsDesc:'عند ظهور شبكة واي فاي المولّدة (تشغيل مولّدة الحي)، يرسل الجهاز أمراً للمكيف تلقائياً.',
gsDis:'معطَّل',gsOff:'إطفاء المكيف',gsEco:'التحويل للوضع الاقتصادي',
gsDelay:'التأخير قبل الإرسال',gsSsid:'اسم شبكة المولّدة',
gsDet:'تم كشف المولّدة',gsNo:'غير مكشوفة',
delayS:['٣ ثوانٍ','٥ ثوانٍ','١٠ ثوانٍ','١٥ ثانية','٣٠ ثانية','دقيقة واحدة','دقيقتان','٥ دقائق'],
wifi:'الواي فاي',status:'الحالة',conn:'متصل',noconn:'غير متصل',
ssid:'اسم الشبكة (SSID)',pass:'كلمة المرور',save:'حفظ',forget:'نسيان الشبكة',
time:'الوقت والساعة',devtime:'وقت الجهاز',ntp:'تلقائي (وقت الإنترنت)',
manual:'ضبط يدوي',tz:'المنطقة الزمنية',apply:'تطبيق',
baghdad:'بغداد / الكويت / الرياض (+3)',dubai:'دبي (+4)',
advanced:'متقدم',factory:'إعادة ضبط المصنع',
factoryMsg:'مسح كل شيء (الأكواد، الواي فاي، الجدولة)؟',
saved:'تم الحفظ.',err:'خطأ — حاول مجدداً.',lang:'EN',
dayS:['أحد','إثن','ثلا','أرب','خمي','جمع','سبت'],
actS:{on:'تشغيل',off:'إطفاء',eco:'اقتصادي'}}};
var L='ar',ST=null,selDays=[];
var DELAYS=[3,5,10,15,30,60,120,300];   // seconds; labels come from delayS

function t(k){return D[L][k]}
function $(id){return document.getElementById(id)}
function toast(m){var e=$('toast');e.textContent=m;e.style.display='block';
clearTimeout(e._t);e._t=setTimeout(function(){e.style.display='none'},3500)}

function setLang(l){L=l;try{localStorage.setItem('erl',l)}catch(e){}
document.documentElement.lang=l;document.documentElement.dir=(l=='ar')?'rtl':'ltr';
var els=document.querySelectorAll('[data-k]');
for(var i=0;i<els.length;i++)els[i].textContent=t(els[i].getAttribute('data-k'));
$('lang').textContent=t('lang');
var gd=$('g_delay'),cur=gd.value;gd.innerHTML='';
DELAYS.forEach(function(v,i){var o=document.createElement('option');
o.value=v;o.textContent=t('delayS')[i];gd.appendChild(o)});
if(cur)gd.value=cur;
renderDays();render()}

function renderDays(){var b=$('s_days');b.innerHTML='';
for(var i=0;i<7;i++)(function(i){var d=document.createElement('div');
d.className='day'+(selDays.indexOf(i)>=0?' sel':'');d.textContent=t('dayS')[i];
d.onclick=function(){var p=selDays.indexOf(i);
if(p>=0)selDays.splice(p,1);else selDays.push(i);renderDays()};b.appendChild(d)})(i)}

function render(){if(!ST)return;
['on','off','eco'].forEach(function(b){var s=$('st_'+b),
ok=ST.codes&&ST.codes[b]&&ST.codes[b].set;
s.textContent=ok?t('recorded'):t('empty');s.className='st'+(ok?' on':'')});
var w=ST.wifi||{};$('wst').textContent=w.connected?t('conn'):t('noconn');
$('wst').style.color=w.connected?'var(--ok)':'var(--bad)';
$('wip').textContent=w.connected?w.ip:'-';
if(!$('w_ssid').value&&w.ssid)$('w_ssid').value=w.ssid;
var tt=ST.time||{};
$('now').textContent=tt.valid?new Date(tt.epoch*1000).toLocaleString(L=='ar'?'ar':'en-GB'):'--:--';
$('clock').textContent=$('now').textContent;
var g=ST.genset||{};
if(g.mode=='off'||g.mode=='eco'){
$('gdet').textContent=g.detected?t('gsDet'):t('gsNo');
$('gdet').style.color=g.detected?'var(--warn)':'var(--mut)';
}else{$('gdet').textContent='—';$('gdet').style.color='var(--mut)'}
var sl=$('slist');sl.innerHTML='';var arr=ST.schedules||[];
if(!arr.length){sl.innerHTML='<div class="empty">'+t('none')+'</div>';return}
arr.forEach(function(s){var d=document.createElement('div');d.className='sched';
var days=(s.days||[]).map(function(i){return t('dayS')[i]}).join(' ');
d.innerHTML='<div><b>'+t('actS')[s.action]+'</b> '+
String(s.hour).padStart(2,'0')+':'+String(s.min).padStart(2,'0')+
'<div class="meta">'+days+'</div></div>';
var x=document.createElement('button');x.className='del';x.textContent='×';
x.onclick=function(){delSched(s.id)};d.appendChild(x);sl.appendChild(d)})}

var first=true;
async function refresh(){try{var r=await fetch('/api/status');ST=await r.json();
if(first){first=false;var tt=ST.time||{};
if(tt.tz)$('t_tz').value=tt.tz;
$('tm_ntp').checked=!!tt.ntp;$('tm_man').checked=!tt.ntp;manualBox();
var g=ST.genset||{};
var el=$('gs_'+(g.mode=='off'?'off':g.mode=='eco'?'eco':'dis'));if(el)el.checked=true;
if(g.delay&&DELAYS.indexOf(g.delay)>=0)$('g_delay').value=g.delay;
if(g.ssid)$('g_ssid').value=g.ssid}
render()}catch(e){}}

async function doSend(b){if(ST&&ST.codes&&ST.codes[b]&&!ST.codes[b].set){toast(t('noCode'));return}
try{var r=await fetch('/api/send?btn='+b,{method:'POST'});
toast(r.ok?t('queued'):t('noCode'))}catch(e){toast(t('err'))}}

async function doRec(b){var m=$('recmsg');
var was=!!(ST&&ST.codes&&ST.codes[b]&&ST.codes[b].set);
try{await fetch('/api/record?btn='+b,{method:'POST'})}catch(e){toast(t('err'));return}
m.textContent=t('recording');m.style.display='block';
var n=0,iv=setInterval(async function(){n++;await refresh();
var now=!!(ST&&ST.codes&&ST.codes[b]&&ST.codes[b].set);
if(!was&&now){clearInterval(iv);m.style.display='none';toast(t('recOk'));return}
if(n>=16){clearInterval(iv);m.style.display='none';
toast(was?t('saved'):t('recFail'))}},1000)}

async function addSched(){if(!selDays.length){toast(t('pickDay'));return}
var tm=$('s_time').value.split(':');
try{var r=await fetch('/api/schedule',{method:'POST',
body:JSON.stringify({action:$('s_act').value,hour:+tm[0],min:+tm[1],days:selDays})});
if(r.ok){toast(t('saved'));refresh()}else toast(t('err'))}catch(e){toast(t('err'))}}
async function delSched(id){try{await fetch('/api/schedule?id='+id,{method:'DELETE'});
refresh()}catch(e){toast(t('err'))}}

async function wifiSave(){try{var r=await fetch('/api/wifi',{method:'POST',
body:JSON.stringify({ssid:$('w_ssid').value,pass:$('w_pass').value})});
toast(r.ok?t('saved'):t('err'));setTimeout(refresh,4000)}catch(e){toast(t('err'))}}
async function wifiForget(){try{await fetch('/api/wifi',{method:'DELETE'});
$('w_ssid').value='';$('w_pass').value='';toast(t('saved'));refresh()}catch(e){toast(t('err'))}}

async function gsSave(){
var m=document.querySelector('input[name="gs"]:checked').value;
try{var r=await fetch('/api/genset',{method:'POST',body:JSON.stringify(
{mode:m,delay:+$('g_delay').value,ssid:$('g_ssid').value||'GENSET_ACTIVE'})});
toast(r.ok?t('saved'):t('err'));setTimeout(refresh,500)}catch(e){toast(t('err'))}}

function manualBox(){$('manbox').style.display=$('tm_man').checked?'block':'none'}
async function timeSave(){var body={ntp:$('tm_ntp').checked,tz:$('t_tz').value};
if(!body.ntp){if(!$('t_iso').value){toast(t('err'));return}body.iso=$('t_iso').value}
try{var r=await fetch('/api/time',{method:'POST',body:JSON.stringify(body)});
toast(r.ok?t('saved'):t('err'));setTimeout(refresh,1500)}catch(e){toast(t('err'))}}

async function factory(){if(!confirm(t('factoryMsg')))return;
try{await fetch('/api/factory-reset',{method:'POST'})}catch(e){}
setTimeout(function(){location.reload()},4000)}

var s='ar';try{s=localStorage.getItem('erl')||'ar'}catch(e){}
setLang(s);refresh();setInterval(refresh,5000);
</script></body></html>
)PORTAL";
