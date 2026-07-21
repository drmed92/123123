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
--txt:#f2f6fb;--mut:#b3c0d4;--ok:#86efac;--bad:#fca5a5;--warn:#fcd34d}
*{box-sizing:border-box}
body{margin:0;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Tahoma,sans-serif;
font-size:16px;background:var(--bg);color:var(--txt);min-height:100vh;padding:16px 16px 40px}
.wrap{max-width:440px;margin:0 auto}
header{display:flex;align-items:center;gap:12px;margin-bottom:16px}
.logo{width:40px;height:40px;border-radius:12px;flex:none;
background:linear-gradient(135deg,var(--b),var(--b2));display:grid;place-items:center}
.logo svg{width:22px;height:22px;color:#fff}
header h1{font-size:18px;margin:0;flex:1}
#clock{font-size:13px;color:var(--mut)}
.lang{padding:7px 15px;font-size:14px;font-weight:600;background:var(--line);
border:0;border-radius:10px;color:var(--txt);cursor:pointer;font-family:inherit}
section{background:var(--card);border:1px solid var(--line);border-radius:16px;
padding:16px;margin-bottom:14px}
h2{font-size:15px;margin:0 0 12px;color:var(--mut);font-weight:600;
text-transform:uppercase;letter-spacing:.04em}
.row{display:flex;align-items:center;justify-content:space-between;gap:10px;
padding:10px 0;border-top:1px solid var(--line)}
.row:first-of-type{border-top:0}
.bt{font-weight:700;font-size:17px}
.st{font-size:13.5px;color:var(--mut)}
.st.on{color:var(--ok)}
.acts{display:flex;gap:8px;flex:none}
button{border:0;border-radius:12px;padding:11px 17px;font-size:16px;font-weight:700;
color:#fff;background:var(--b);cursor:pointer;font-family:inherit}
button.sec{background:var(--line)}
button.danger{background:#7f1d1d;width:100%}
button:disabled{opacity:.5}
.msg{font-size:15px;color:var(--warn);margin-top:10px;display:none}
input,select{width:100%;padding:12px;margin-bottom:10px;border:1px solid var(--line);
border-radius:10px;background:var(--bg);color:var(--txt);font-size:16.5px;font-family:inherit}
label{display:block;font-size:13.5px;color:var(--mut);margin-bottom:4px}
.inline{display:flex;gap:8px}
.inline>*{flex:1}
.days{display:flex;gap:6px;margin-bottom:12px;flex-wrap:wrap}
.day{flex:1;min-width:38px;padding:9px 0;text-align:center;font-size:13.5px;font-weight:600;
border:1px solid var(--line);border-radius:9px;background:var(--bg);color:var(--mut);
cursor:pointer;user-select:none}
.day.sel{background:var(--b);border-color:var(--b);color:#fff}
.sched{display:flex;align-items:center;justify-content:space-between;gap:8px;
padding:10px 0;border-bottom:1px solid var(--line);font-size:16px}
.sched .del{background:none;color:var(--bad);font-size:18px;padding:2px 10px}
.sched .meta{color:var(--mut);font-size:13.5px}
.empty{color:var(--mut);font-size:15px;padding:6px 0}
.kv{display:flex;justify-content:space-between;font-size:15px;padding:4px 0}
.kv span:first-child{color:var(--mut)}
.radio{display:flex;align-items:center;gap:9px;margin-bottom:11px;font-size:16px}
.radio input{width:auto;margin:0}
.desc{font-size:15px;color:var(--mut);margin:0 0 10px;line-height:1.5}
.linkrow{display:flex;gap:8px}
.linkrow input{direction:ltr;text-align:left;font-size:14px;margin-bottom:0}
.linkrow button{flex:none}
.led{width:14px;height:14px;border-radius:50%;background:#4b5563;flex:none;
display:inline-block;transition:background .3s,box-shadow .3s}
.led.on{background:#22c55e;box-shadow:0 0 12px rgba(34,197,94,.85);
animation:ledpulse 1.6s ease-in-out infinite}
@keyframes ledpulse{50%{box-shadow:0 0 4px rgba(34,197,94,.35)}}
#toast{position:fixed;bottom:18px;left:50%;transform:translateX(-50%);
background:#1e293b;border:1px solid var(--line);color:var(--txt);padding:11px 18px;
border-radius:12px;font-size:15px;display:none;max-width:90vw;box-shadow:0 8px 24px rgba(0,0,0,.5)}
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
  <h2 style="display:flex;align-items:center;gap:9px"><span class="led" id="rled"></span>
  <span data-k="rTitle"></span></h2>
  <p class="desc" data-k="rDesc"></p>
  <div class="kv"><span data-k="status"></span><span id="rst">-</span></div>
  <div id="rlinkbox" style="display:none;margin-top:8px">
    <label data-k="rLink"></label>
    <div class="linkrow"><input id="rlink" readonly>
    <button class="sec" onclick="copyLink()" data-k="copy"></button></div>
    <p id="rwarn" data-k="rWarn" style="color:var(--bad);font-weight:700;font-size:14px;
    line-height:1.6;margin:8px 0 0"></p>
  </div>
  <p class="desc" id="rwait" data-k="rWait" style="display:none;margin:8px 0 0"></p>
  <p class="desc" id="rerr" style="display:none;margin:6px 0 0;color:var(--warn)"></p>
</section>

<section>
  <h2 style="display:flex;align-items:center;gap:9px"><span class="led" id="gled"></span>
  <span data-k="genset"></span></h2>
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
    <label data-k="ssid"></label>
    <div class="linkrow"><select id="w_sel" onchange="wSel()"></select>
    <button class="sec" onclick="wScan(1)" data-k="rescan"></button></div>
    <input id="w_ssid" maxlength="32" style="display:none">
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
  <button class="sec" style="width:100%;margin-bottom:10px"
    onclick="location.href='/?setup=1'" data-k="wizBtn"></button>
  <button class="danger" onclick="factory()" data-k="factory"></button>
</section>

</div><div id="toast"></div><script>
var D={
en:{remote:'Remote',on:'Turn ON',off:'Turn OFF',eco:'ECO mode',send:'Send',rec:'Record',
recorded:'Recorded',empty:'Not recorded',noCode:'Record this button first.',
recording:'Point the AC remote at the device and press its button now…',
recOk:'Code recorded!',recFail:'Nothing received — try again.',
recOvf:'Signal too long for the buffer — try again from ~30 cm away.',
queued:'Command sent.',
schedules:'Schedules',action:'Action',at:'Time',days:'Days',add:'Add schedule',
none:'No schedules yet.',pickDay:'Pick at least one day.',
rTitle:'Remote access',
rDesc:'Control this AC from anywhere over the internet using your personal link.',
rLink:'Your personal link',copy:'Copy',copied:'Link copied.',
rWarn:'⚠ Save this link! The ERemote Wi-Fi turns off a few minutes after setup to rest the device; afterwards you reach the AC through this link.',
rOn:'Connected to server',rOff:'Not connected',
rWait:'Waiting for internet connection to set up remote access…',
rErrNet:'Cannot reach the server — check the server address, that it is running, and that port 80 is open.',
rErr403:'The server refused this device (identity conflict) — an old registration for this chip must be deleted on the server.',
rErrHttp:'Server error (HTTP ',
genset:'AutoGenset',
gsDesc:"When the generator's Wi-Fi network appears (neighborhood genset switched on), the device automatically sends a command to the AC.",
gsDis:'Disabled',gsOff:'Turn AC OFF',gsEco:'Switch to ECO',
gsDelay:'Delay before sending',gsSsid:'Generator network name',
gsDet:'Generator detected',gsNo:'Not detected',
delayS:['3 seconds','5 seconds','10 seconds','15 seconds','30 seconds','1 minute','2 minutes','5 minutes'],
wifi:'Wi-Fi',status:'Status',conn:'Connected',noconn:'Not connected',
ssid:'Wi-Fi name',pass:'Password',save:'Save',forget:'Forget network',
rescan:'Refresh',otherNet:'Other network…',wizBtn:'Run setup wizard',
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
recOvf:'الإشارة أطول من الذاكرة — حاول من مسافة ٣٠ سم تقريباً.',
queued:'تم إرسال الأمر.',
schedules:'الجدولة',action:'الإجراء',at:'الوقت',days:'الأيام',add:'إضافة جدولة',
none:'لا توجد جدولات بعد.',pickDay:'اختر يوماً واحداً على الأقل.',
rTitle:'التحكم عن بُعد',
rDesc:'تحكم بهذا المكيف من أي مكان عبر الإنترنت باستخدام رابطك الخاص.',
rLink:'رابطك الخاص',copy:'نسخ',copied:'تم نسخ الرابط.',
rWarn:'⚠ احفظ هذا الرابط! تنطفئ شبكة ERemote بعد دقائق من الإعداد لإراحة الجهاز؛ بعدها تصل للمكيف عبر هذا الرابط.',
rOn:'متصل بالخادم',rOff:'غير متصل',
rWait:'بانتظار الاتصال بالإنترنت لإعداد التحكم عن بُعد…',
rErrNet:'تعذّر الوصول للخادم — تحقق من عنوان الخادم وأنه يعمل وأن المنفذ 80 مفتوح.',
rErr403:'رفض الخادم هذا الجهاز (تعارض هوية) — يجب حذف التسجيل القديم لهذه الشريحة من الخادم.',
rErrHttp:'خطأ من الخادم (HTTP ',
genset:'كشف المولّدة تلقائياً',
gsDesc:'عند ظهور شبكة واي فاي المولّدة (تشغيل مولّدة الحي)، يرسل الجهاز أمراً للمكيف تلقائياً.',
gsDis:'معطَّل',gsOff:'إطفاء المكيف',gsEco:'التحويل للوضع الاقتصادي',
gsDelay:'التأخير قبل الإرسال',gsSsid:'اسم شبكة المولّدة',
gsDet:'تم كشف المولّدة',gsNo:'غير مكشوفة',
delayS:['٣ ثوانٍ','٥ ثوانٍ','١٠ ثوانٍ','١٥ ثانية','٣٠ ثانية','دقيقة واحدة','دقيقتان','٥ دقائق'],
wifi:'الواي فاي',status:'الحالة',conn:'متصل',noconn:'غير متصل',
ssid:'اسم شبكة الواي فاي',pass:'كلمة المرور',save:'حفظ',forget:'نسيان الشبكة',
rescan:'تحديث',otherNet:'شبكة أخرى…',wizBtn:'إعادة تشغيل معالج الإعداد',
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
var so=$('w_sel');
if(so)for(var i=0;i<so.options.length;i++)
if(so.options[i].value=='__other')so.options[i].textContent=t('otherNet');
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
var tt=ST.time||{};
$('now').textContent=tt.valid?new Date(tt.epoch*1000).toLocaleString(L=='ar'?'ar':'en-GB'):'--:--';
$('clock').textContent=$('now').textContent;
var g=ST.genset||{};
$('gled').className='led'+(g.detected?' on':'');
$('gdet').textContent=g.detected?t('gsDet'):t('gsNo');
$('gdet').style.color=g.detected?'var(--ok)':'var(--mut)';
var r=ST.remote||{};
$('rled').className='led'+(r.mqtt?' on':'');
$('rst').textContent=r.mqtt?t('rOn'):t('rOff');
$('rst').style.color=r.mqtt?'var(--ok)':'var(--mut)';
if(r.claimed&&r.code){$('rlinkbox').style.display='block';
$('rwait').style.display='none';$('rerr').style.display='none';
$('rlink').value='https://er.my.to/r/'+r.code}   // always show the domain link
else{$('rlinkbox').style.display='none';$('rwait').style.display='block';
var rc=r.lastRc|0,e=$('rerr');
if(rc===0){e.style.display='none'}
else{e.style.display='block';
e.textContent=rc<0?t('rErrNet'):rc==403?t('rErr403'):t('rErrHttp')+rc+')'}}
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
if(g.ssid)$('g_ssid').value=g.ssid;
wScan(1)}
render()}catch(e){}}

async function doSend(b){if(ST&&ST.codes&&ST.codes[b]&&!ST.codes[b].set){toast(t('noCode'));return}
try{var r=await fetch('/api/send?btn='+b,{method:'POST'});
toast(r.ok?t('queued'):t('noCode'))}catch(e){toast(t('err'))}}

var recIv=null;
async function doRec(b){var m=$('recmsg');
if(recIv){clearInterval(recIv);recIv=null}   // drop any previous button's window
var was=!!(ST&&ST.codes&&ST.codes[b]&&ST.codes[b].set);
try{await fetch('/api/record?btn='+b,{method:'POST'})}catch(e){toast(t('err'));return}
m.textContent=t('recording')+' (30)';m.style.display='block';
var n=0,iv=recIv=setInterval(async function(){n++;
m.textContent=t('recording')+' ('+Math.max(0,30-n)+')';
await refresh();
var now=!!(ST&&ST.codes&&ST.codes[b]&&ST.codes[b].set);
var lc=ST&&ST.lastCapture;
var info=(lc&&lc.btn==b&&lc.proto)?' ('+lc.proto+', '+lc.len+')':'';
if(!was&&now){clearInterval(iv);m.style.display='none';toast(t('recOk')+info);return}
if(n>=30){clearInterval(iv);m.style.display='none';
toast(was?t('saved')+info:(lc&&lc.btn==b&&lc.overflow?t('recOvf'):t('recFail')))}},1000)}

async function addSched(){if(!selDays.length){toast(t('pickDay'));return}
var tm=$('s_time').value.split(':');
try{var r=await fetch('/api/schedule',{method:'POST',
body:JSON.stringify({action:$('s_act').value,hour:+tm[0],min:+tm[1],days:selDays})});
if(r.ok){toast(t('saved'));refresh()}else toast(t('err'))}catch(e){toast(t('err'))}}
async function delSched(id){try{await fetch('/api/schedule?id='+id,{method:'DELETE'});
refresh()}catch(e){toast(t('err'))}}

var scanTries=0;
async function wScan(force){
if(force)scanTries=0;
try{var r=await fetch('/api/scan');var s=await r.json();
var sel=$('w_sel'),cur=sel.value;
if(s.networks&&s.networks.length){
s.networks.sort(function(a,b){return b.db-a.db});
sel.innerHTML='';
s.networks.forEach(function(nw){var o=document.createElement('option');
o.value=nw.n;o.textContent=nw.n;sel.appendChild(o)});
var oo=document.createElement('option');oo.value='__other';
oo.textContent=t('otherNet');sel.appendChild(oo);
var want=cur&&cur!='__other'?cur:((ST&&ST.wifi&&ST.wifi.ssid)||'');
if(want){var found=false;
for(var i=0;i<sel.options.length;i++)if(sel.options[i].value===want){found=true;break}
if(!found){var o2=document.createElement('option');o2.value=want;
o2.textContent=want;sel.insertBefore(o2,sel.firstChild)}
sel.value=want}
wSel()}
else if(scanTries++<6)setTimeout(function(){wScan(0)},2000)
}catch(e){}}
function wSel(){$('w_ssid').style.display=($('w_sel').value=='__other')?'block':'none'}
function wName(){var sel=$('w_sel');
return (sel&&sel.value&&sel.value!='__other')?sel.value:$('w_ssid').value}

async function wifiSave(){var n=wName();if(!n){toast(t('err'));return}
try{var r=await fetch('/api/wifi',{method:'POST',
body:JSON.stringify({ssid:n,pass:$('w_pass').value})});
toast(r.ok?t('saved'):t('err'));setTimeout(refresh,4000)}catch(e){toast(t('err'))}}
async function wifiForget(){try{await fetch('/api/wifi',{method:'DELETE'});
$('w_ssid').value='';$('w_pass').value='';toast(t('saved'));refresh()}catch(e){toast(t('err'))}}

async function gsSave(){
var m=document.querySelector('input[name="gs"]:checked').value;
try{var r=await fetch('/api/genset',{method:'POST',body:JSON.stringify(
{mode:m,delay:+$('g_delay').value,ssid:$('g_ssid').value||'GENSET_ACTIVE'})});
toast(r.ok?t('saved'):t('err'));setTimeout(refresh,500)}catch(e){toast(t('err'))}}

function copyLink(){var i=$('rlink');i.select();i.setSelectionRange(0,200);
try{navigator.clipboard.writeText(i.value).then(function(){toast(t('copied'))},
function(){document.execCommand('copy');toast(t('copied'))})}
catch(e){document.execCommand('copy');toast(t('copied'))}}

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
