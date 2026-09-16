/* ntp_portal.h -- NtpEremote programming page (served in the 3-min AP window).
   Enter home Wi-Fi (for NTP), record ON/OFF/ECO, edit the weekly schedule,
   then sleep. Bilingual Arabic-default; talks to the /api/* endpoints. */
#pragma once

const char SETUP_HTML[] PROGMEM = R"TR(
<!DOCTYPE html><html lang="ar" dir="rtl"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>NtpEremote</title><style>
:root{--b:#0f766e;--b2:#0ea5a4;--bg:#0b1220;--card:#131c2b;--line:#243244;
--txt:#f2f6fb;--mut:#b3c0d4;--ok:#86efac;--bad:#fca5a5;--warn:#fcd34d}
*{box-sizing:border-box}
body{margin:0;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Tahoma,sans-serif;
font-size:16px;background:var(--bg);color:var(--txt);min-height:100vh;padding:16px 16px 40px}
.wrap{max-width:440px;margin:0 auto}
header{display:flex;align-items:center;gap:12px;margin-bottom:14px}
.logo{width:40px;height:40px;border-radius:12px;flex:none;
background:linear-gradient(135deg,var(--b),var(--b2));display:grid;place-items:center;font-size:22px}
header h1{font-size:18px;margin:0;flex:1}
#apl{font-size:12px;color:var(--warn)}
.lang{padding:7px 14px;font-size:14px;font-weight:600;background:var(--line);border:0;
border-radius:10px;color:var(--txt);cursor:pointer;font-family:inherit}
section{background:var(--card);border:1px solid var(--line);border-radius:16px;padding:16px;margin-bottom:14px}
h2{font-size:15px;margin:0 0 12px;color:var(--mut);font-weight:600;text-transform:uppercase;letter-spacing:.04em}
.row{display:flex;align-items:center;justify-content:space-between;gap:10px;padding:10px 0;border-top:1px solid var(--line)}
.row:first-of-type{border-top:0}
.bt{font-weight:700;font-size:17px}.st{font-size:13.5px;color:var(--mut)}.st.on{color:var(--ok)}
.acts{display:flex;gap:8px;flex:none}
button{border:0;border-radius:12px;padding:11px 16px;font-size:16px;font-weight:700;color:#fff;
background:var(--b);cursor:pointer;font-family:inherit}
button.sec{background:var(--line)}button.danger{background:#7f1d1d}
input,select{width:100%;padding:12px;margin-bottom:10px;border:1px solid var(--line);border-radius:10px;
background:var(--bg);color:var(--txt);font-size:16.5px;font-family:inherit}
label{display:block;font-size:13.5px;color:var(--mut);margin-bottom:4px}
.inline{display:flex;gap:8px}.inline>*{flex:1}
.chk{display:flex;align-items:center;gap:9px;margin:6px 0 2px;font-size:14px;color:var(--txt)}
.chk input{width:auto;margin:0;transform:scale(1.2)}
.days{display:flex;gap:6px;margin:6px 0 12px;flex-wrap:wrap}
.day{flex:1;min-width:36px;padding:9px 0;text-align:center;font-size:13.5px;font-weight:600;
border:1px solid var(--line);border-radius:9px;background:var(--bg);color:var(--mut);cursor:pointer;user-select:none}
.day.sel{background:var(--b);border-color:var(--b);color:#fff}
.sched{display:flex;align-items:center;justify-content:space-between;gap:8px;padding:10px 0;
border-bottom:1px solid var(--line);font-size:16px}
.sched .del{background:none;color:var(--bad);font-size:20px;padding:2px 10px}
.sched .meta{color:var(--mut);font-size:13.5px}
.empty{color:var(--mut);font-size:15px;padding:6px 0}
.msg{font-size:14px;color:var(--warn);margin-top:10px;display:none}
.kv{display:flex;justify-content:space-between;font-size:15px;margin-bottom:10px}
.pill{font-size:12.5px;padding:3px 10px;border-radius:999px;background:var(--line);color:var(--mut)}
.pill.ok{background:#14532d;color:var(--ok)}.pill.bad{background:#7f1d1d;color:var(--bad)}
#toast{position:fixed;bottom:18px;left:50%;transform:translateX(-50%);background:#1e293b;
border:1px solid var(--line);color:var(--txt);padding:12px 18px;border-radius:12px;font-size:15px;display:none;max-width:90vw}
</style></head><body><div class="wrap">

<header>
  <div class="logo">🕒</div>
  <h1>NtpEremote <div id="apl"></div></h1>
  <button class="lang" id="lang" onclick="setLang(L=='en'?'ar':'en')">EN</button>
</header>

<section>
  <h2 data-k="wifiT"></h2>
  <div class="kv"><span data-k="devtime"></span><span id="now">-</span></div>
  <div class="kv"><span data-k="ntpState"></span><span id="ntp" class="pill" data-k="ntpNone"></span></div>
  <label data-k="ssidL"></label>
  <input id="w_ssid" type="text" autocomplete="off" placeholder="MyHomeWiFi">
  <label data-k="passL"></label>
  <input id="w_pass" type="password" autocomplete="off" placeholder="••••••••">
  <button style="width:100%" onclick="saveWifi()" data-k="wifiSave"></button>
  <p style="color:var(--mut);font-size:13px;line-height:1.6;margin:10px 0 0" data-k="wifiNote"></p>
</section>

<section>
  <h2 data-k="remote"></h2>
  <div class="row"><div><div class="bt" data-k="on"></div><div class="st" id="st_on"></div></div>
    <div class="acts"><button onclick="doSend('on')" data-k="test"></button>
    <button class="sec" onclick="doRec('on')" data-k="rec"></button></div></div>
  <div class="row"><div><div class="bt" data-k="off"></div><div class="st" id="st_off"></div></div>
    <div class="acts"><button onclick="doSend('off')" data-k="test"></button>
    <button class="sec" onclick="doRec('off')" data-k="rec"></button></div></div>
  <div class="row"><div><div class="bt" data-k="eco"></div><div class="st" id="st_eco"></div></div>
    <div class="acts"><button onclick="doSend('eco')" data-k="test"></button>
    <button class="sec" onclick="doRec('eco')" data-k="rec"></button></div></div>
  <label class="chk"><input type="checkbox" id="ecoon" onchange="saveCfg()"><span data-k="ecoOn"></span></label>
  <label class="chk"><input type="checkbox" id="ledon" onchange="saveCfg()"><span data-k="ledOn"></span></label>
  <div class="msg" id="recmsg"></div>
</section>

<section>
  <h2 data-k="schedules"></h2>
  <div id="slist"></div>
  <div style="margin-top:12px">
    <div class="inline">
      <div><label data-k="action"></label>
        <select id="s_act"><option value="on" data-k="on"></option>
        <option value="off" data-k="off"></option><option value="eco" data-k="eco"></option></select></div>
      <div><label data-k="at"></label><input id="s_time" type="time" value="14:00"></div>
    </div>
    <label data-k="days"></label><div class="days" id="s_days"></div>
    <button style="width:100%" onclick="addSched()" data-k="add"></button>
  </div>
</section>

<section>
  <h2 data-k="powerT"></h2>
  <p style="color:var(--mut);font-size:14px;line-height:1.7;margin:0 0 12px" data-k="sleepNote"></p>
  <button class="danger" style="width:100%" onclick="sleepNow()" data-k="sleepBtn"></button>
</section>

</div><div id="toast"></div><script>
var D={
en:{wifiT:'Wi-Fi & time',devtime:'Device time',ntpState:'Internet time',
ntpNone:'not checked',ntpOk:'synced',ntpBad:'failed',
ssidL:'Home Wi-Fi name (2.4 GHz)',passL:'Wi-Fi password',wifiSave:'Save Wi-Fi & sync time',
wifiNote:'The device only needs Wi-Fi briefly to read the real time from the internet (Baghdad, UTC+3). It must be a 2.4 GHz network.',
remote:'Remote buttons',on:'ON',off:'OFF',eco:'ECO',test:'Test',rec:'Record',
recorded:'Recorded',empty:'Not recorded',recOk:'Recorded!',recFail:'Nothing received — try again.',
recWait:'Point the remote at the sensor and press the button…',
ecoOn:'ECO: turn the AC on first, then ECO (needed on most units)',
ledOn:'Blink the LED when an IR command is sent',
schedules:'Schedule',action:'Action',at:'Time',days:'Days',add:'Add schedule',
none:'No schedules yet.',pickDay:'Pick at least one day.',
powerT:'Power',sleepNote:'When you finish, the device sleeps to save battery and only wakes to run the schedule. It re-checks internet time shortly before each event, so timing stays accurate. The Wi-Fi turns off automatically after 3 minutes. Press RST to program again.',
sleepBtn:'Save & sleep now',sent:'Sent.',saved:'Saved.',err:'Error — try again.',
syncing:'Saving & syncing… this takes a few seconds.',
sleeping:'Saved. The device is going to sleep — Wi-Fi will disconnect.',
apLeft:'Wi-Fi off in ',lang:'عربي',dayS:['Sun','Mon','Tue','Wed','Thu','Fri','Sat']},
ar:{wifiT:'الواي فاي والوقت',devtime:'وقت الجهاز',ntpState:'وقت الإنترنت',
ntpNone:'لم يُفحص',ntpOk:'تمت المزامنة',ntpBad:'فشل',
ssidL:'اسم شبكة الواي فاي المنزلية (2.4 غيغا)',passL:'كلمة مرور الواي فاي',wifiSave:'حفظ الواي فاي ومزامنة الوقت',
wifiNote:'يحتاج الجهاز للواي فاي لثوانٍ فقط ليقرأ الوقت الحقيقي من الإنترنت (بغداد، UTC+3). يجب أن تكون الشبكة 2.4 غيغاهرتز.',
remote:'أزرار الريموت',on:'تشغيل',off:'إطفاء',eco:'اقتصادي',test:'تجربة',rec:'تسجيل',
recorded:'مسجَّل',empty:'غير مسجَّل',recOk:'تم التسجيل!',recFail:'لم يصل شيء — حاول مجدداً.',
recWait:'وجّه الريموت نحو الحساس واضغط الزر…',
ecoOn:'الاقتصادي: شغّل المكيف أولاً ثم حوّله للاقتصادي (لازم لأغلب المكيفات)',
ledOn:'وميض المؤشر الضوئي عند إرسال أمر',
schedules:'الجدولة',action:'الإجراء',at:'الوقت',days:'الأيام',add:'إضافة جدولة',
none:'لا توجد جدولات بعد.',pickDay:'اختر يوماً واحداً على الأقل.',
powerT:'الطاقة',sleepNote:'عند الانتهاء ينام الجهاز لتوفير البطارية ولا يستيقظ إلا لتنفيذ الجدولة. يعيد فحص وقت الإنترنت قبل كل موعد بقليل لتبقى الدقة عالية. ينطفئ الواي فاي تلقائياً بعد ٣ دقائق. اضغط RST لإعادة البرمجة.',
sleepBtn:'حفظ ونوم الآن',sent:'تم الإرسال.',saved:'تم الحفظ.',err:'خطأ — حاول مجدداً.',
syncing:'جارٍ الحفظ والمزامنة… تستغرق بضع ثوانٍ.',
sleeping:'تم الحفظ. الجهاز ينام الآن — سينقطع الواي فاي.',
apLeft:'إيقاف الواي فاي خلال ',lang:'EN',dayS:['أحد','إثن','ثلا','أرب','خمي','جمع','سبت']}};
var L='ar',ST=null,selDays=[],first=true,recIv=null;
function t(k){return D[L][k]}
function $(id){return document.getElementById(id)}
function toast(m){var e=$('toast');e.textContent=m;e.style.display='block';
clearTimeout(e._t);e._t=setTimeout(function(){e.style.display='none'},3500)}
function p2(n){return String(n).padStart(2,'0')}
function fmt(ep){var d=new Date(ep*1000);return d.getUTCFullYear()+'-'+p2(d.getUTCMonth()+1)+'-'+
p2(d.getUTCDate())+' '+p2(d.getUTCHours())+':'+p2(d.getUTCMinutes())}

function setLang(l){L=l;try{localStorage.setItem('erl',l)}catch(e){}
document.documentElement.lang=l;document.documentElement.dir=(l=='ar')?'rtl':'ltr';
var e=document.querySelectorAll('[data-k]');
for(var i=0;i<e.length;i++)e[i].textContent=t(e[i].getAttribute('data-k'));
$('lang').textContent=t('lang');renderDays();render()}

function renderDays(){var b=$('s_days');b.innerHTML='';
for(var i=0;i<7;i++)(function(i){var d=document.createElement('div');
d.className='day'+(selDays.indexOf(i)>=0?' sel':'');d.textContent=t('dayS')[i];
d.onclick=function(){var p=selDays.indexOf(i);if(p>=0)selDays.splice(p,1);else selDays.push(i);renderDays()};
b.appendChild(d)})(i)}

function render(){if(!ST)return;
['on','off','eco'].forEach(function(b){var s=$('st_'+b),ok=ST.codes&&ST.codes[b];
s.textContent=ok?t('recorded'):t('empty');s.className='st'+(ok?' on':'')});
$('now').textContent=ST.haveTime&&ST.epoch?fmt(ST.epoch):'--';
var n=$('ntp');var r=ST.ntp||'';
n.className='pill'+(r=='ok'?' ok':(r=='fail'?' bad':''));
n.textContent=r=='ok'?t('ntpOk'):(r=='fail'?t('ntpBad'):t('ntpNone'));
if(ST.apLeft!=null&&ST.apLeft>=0)$('apl').textContent=t('apLeft')+ST.apLeft+'s';
var sl=$('slist');sl.innerHTML='';var arr=ST.schedules||[];
if(!arr.length){sl.innerHTML='<div class="empty">'+t('none')+'</div>';}
else arr.forEach(function(s){var d=document.createElement('div');d.className='sched';
var days=(s.days||[]).map(function(i){return t('dayS')[i]}).join(' ');
d.innerHTML='<div><b>'+t(s.action)+'</b> '+p2(s.hour)+':'+p2(s.min)+'<div class="meta">'+days+'</div></div>';
var x=document.createElement('button');x.className='del';x.textContent='×';
x.onclick=function(){delSched(s.id)};d.appendChild(x);sl.appendChild(d)});
if(first){first=false;$('ecoon').checked=!!ST.ecoOn;$('ledon').checked=ST.ledOn!==false;
if(ST.ssid)$('w_ssid').value=ST.ssid}}

async function refresh(){try{var r=await fetch('/api/status');ST=await r.json();render()}catch(e){}}

async function saveWifi(){var s=$('w_ssid').value.trim();if(!s){toast(t('err'));return}
toast(t('syncing'));
try{var r=await fetch('/api/wifi',{method:'POST',
body:JSON.stringify({ssid:s,pass:$('w_pass').value})});
var j=await r.json();toast(j.ntp?t('ntpOk'):t('ntpBad'));setTimeout(refresh,400)}
catch(e){toast(t('err'))}}
async function saveCfg(){try{await fetch('/api/cfg',{method:'POST',
body:JSON.stringify({ecoOn:$('ecoon').checked,ledOn:$('ledon').checked})});
toast(t('saved'))}catch(e){toast(t('err'))}}
async function doSend(b){if(!(ST&&ST.codes&&ST.codes[b])){toast(t('empty'));return}
try{var r=await fetch('/api/send?btn='+b,{method:'POST'});toast(r.ok?t('sent'):t('err'))}catch(e){toast(t('err'))}}

async function doRec(b){var m=$('recmsg');
if(recIv){clearInterval(recIv);recIv=null}
var seq0=(ST&&ST.lastCapture&&ST.lastCapture.seq)||0;
try{await fetch('/api/record?btn='+b,{method:'POST'})}catch(e){toast(t('err'));return}
m.textContent=t('recWait')+' (30)';m.style.display='block';
var n=0;recIv=setInterval(async function(){n++;m.textContent=t('recWait')+' ('+Math.max(0,30-n)+')';
await refresh();var lc=ST&&ST.lastCapture;
if(lc&&lc.seq!==seq0&&lc.btn==b){clearInterval(recIv);recIv=null;m.style.display='none';
toast(t('recOk')+(lc.proto?' ('+lc.proto+')':''));return}
if(n>=30){clearInterval(recIv);recIv=null;m.style.display='none';toast(t('recFail'))}},1000)}

async function addSched(){if(!selDays.length){toast(t('pickDay'));return}
var tm=$('s_time').value.split(':');
try{var r=await fetch('/api/schedule',{method:'POST',
body:JSON.stringify({action:$('s_act').value,hour:+tm[0],min:+tm[1],days:selDays})});
if(r.ok){toast(t('saved'));selDays=[];renderDays();refresh()}else toast(t('err'))}catch(e){toast(t('err'))}}
async function delSched(id){try{await fetch('/api/schedule?id='+id,{method:'DELETE'});refresh()}catch(e){toast(t('err'))}}

async function sleepNow(){try{await fetch('/api/sleep',{method:'POST'})}catch(e){}
toast(t('sleeping'))}

var s='ar';try{s=localStorage.getItem('erl')||'ar'}catch(e){}
setLang(s);refresh();setInterval(refresh,2000);
</script></body></html>
)TR";
