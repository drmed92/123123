/* wizard_html.h -- ERemote first-run setup wizard (served as SETUP_HTML when
   the device is not provisioned, or on demand at /?setup=1).
   Step-by-step, Arabic default, RTL, no jargon. Steps: language -> record
   ON -> record OFF -> record ECO (skippable) -> Wi-Fi from a scanned list
   (skippable) -> generator (optional) -> done.
   Uses /api/record, /api/send, /api/status, /api/scan, /api/wifi,
   /api/genset, /api/init. */
#pragma once

const char SETUP_HTML[] PROGMEM = R"WIZ(
<!DOCTYPE html><html lang="ar" dir="rtl"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ERemote Setup</title><style>
:root{--b:#0f766e;--b2:#0ea5a4;--bg:#0b1220;--card:#131c2b;--line:#243244;
--txt:#f2f6fb;--mut:#b3c0d4;--ok:#86efac;--bad:#fca5a5;--warn:#fcd34d}
*{box-sizing:border-box}
body{margin:0;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Tahoma,sans-serif;
font-size:17px;background:var(--bg);color:var(--txt);min-height:100vh;padding:16px;
display:flex;justify-content:center;align-items:flex-start}
.card{max-width:420px;width:100%;background:var(--card);border:1px solid var(--line);
border-radius:20px;padding:24px;margin-top:12px;box-shadow:0 10px 30px rgba(0,0,0,.4);
position:relative}
.lang{position:absolute;top:14px;inset-inline-end:14px;padding:5px 12px;font-size:12px;
font-weight:600;background:var(--line);border:0;border-radius:10px;color:var(--txt);
cursor:pointer;font-family:inherit;display:none}
.dots{display:flex;gap:7px;justify-content:center;margin:2px 0 18px}
.dot{width:9px;height:9px;border-radius:50%;background:#28374b}
.dot.act{background:var(--b2);box-shadow:0 0 8px rgba(14,165,164,.7)}
.dot.done{background:#3d6b52}
h1{font-size:24px;margin:0 0 10px;text-align:center}
p{color:var(--mut);font-size:16.5px;line-height:1.8;margin:0 0 16px;text-align:center}
.logo{width:56px;height:56px;border-radius:16px;margin:4px auto 14px;
background:linear-gradient(135deg,var(--b),var(--b2));display:grid;place-items:center}
.logo svg{width:30px;height:30px;color:#fff}
button{border:0;border-radius:14px;padding:15px;font-size:17px;font-weight:700;
color:#fff;background:var(--b);cursor:pointer;font-family:inherit}
button:disabled{opacity:.5}
.pri{width:100%;margin-top:6px}
.sec{width:100%;margin-top:10px;background:var(--line)}
.ghost{display:block;width:100%;background:none;color:var(--mut);font-weight:600;
font-size:15px;margin-top:12px;text-decoration:underline;padding:8px}
.big2{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-top:6px}
.big2 button{padding:19px 0;font-size:19px}
.pulse{width:78px;height:78px;border-radius:50%;margin:20px auto;position:relative;
background:#0e2233;display:grid;place-items:center;font-size:30px}
.pulse::after{content:'';position:absolute;inset:0;border-radius:50%;
border:2px solid var(--b2);animation:pw 1.5s ease-out infinite}
@keyframes pw{0%{transform:scale(.75);opacity:1}100%{transform:scale(1.55);opacity:0}}
.okmark{width:78px;height:78px;border-radius:50%;margin:20px auto;background:#123527;
display:grid;place-items:center;font-size:36px;color:var(--ok)}
.st{text-align:center;font-size:16.5px;min-height:1.4em;margin:4px 0 10px}
.st.ok{color:var(--ok)}.st.bad{color:var(--bad)}.st.warn{color:var(--warn)}
.count{text-align:center;font-size:34px;font-weight:800;color:var(--b2);
margin:2px 0 8px;font-variant-numeric:tabular-nums}
input,select{width:100%;padding:13px;margin:6px 0 10px;border:1px solid var(--line);
border-radius:10px;background:var(--bg);color:var(--txt);font-size:17px;font-family:inherit}
label{display:block;font-size:14px;color:var(--mut)}
.netlist{border:1px solid var(--line);border-radius:12px;overflow:hidden;margin:6px 0}
.net{display:flex;justify-content:space-between;align-items:center;gap:10px;
padding:15px 13px;border-bottom:1px solid var(--line);cursor:pointer;font-size:17px}
.net:last-child{border-bottom:0}
.net:active{background:#1a2534}
.net .m{color:var(--mut);font-size:13px;direction:ltr;flex:none}
.radio{display:flex;align-items:center;gap:9px;margin:12px 0;font-size:17px}
.radio input{width:auto;margin:0;transform:scale(1.25)}
.sum{font-size:16px;line-height:2.1;margin:0 0 14px}
.sum b{color:var(--ok)}
.spin{width:26px;height:26px;border:3px solid var(--line);border-top-color:var(--b2);
border-radius:50%;margin:14px auto;animation:sp 1s linear infinite}
@keyframes sp{to{transform:rotate(360deg)}}
.linkrow{display:flex;gap:8px;margin:4px 0 10px}
.linkrow input{direction:ltr;text-align:left;font-size:15px;margin:0}
.linkrow button{flex:none;background:var(--line)}
</style></head><body>
<div class="card">
  <button class="lang" id="langbtn" onclick="setLang(L=='en'?'ar':'en',true)">EN</button>
  <div class="dots" id="dots" style="display:none"></div>
  <div id="scr"></div>
</div>
<script>
var D={
en:{
 bn_on:'ON',bn_off:'OFF',bn_eco:'ECO',
 wTitle:'Welcome to ERemote',wSub:'Your smart AC remote. Setup takes about two minutes. Choose your language:',
 recTitle:'Record the {B} button',
 recHow:'Bring the AC remote control. Point it at the black dot on the device, then press the {B} button once.',
 listening:'Listening… press the button now',
 recOk:'Recorded successfully!',
 recPre:'This button is already recorded.',
 recFail:'Nothing received. Point the remote at the black dot and try again.',
 recOvf:'Signal too long — try again from about 30 cm away.',
 test:'Test it on the AC',testSent:'Sent — did the AC respond?',
 rerec:'Record again',retry:'Try again',next:'Next',back:'Back',
 skipEco:'My remote has no ECO button — skip',
 skipRec:'Skip this button — set it up later',
 fTitle:'Connect to home Wi-Fi',
 fSub:'Choose your home Wi-Fi so the device gets the correct time and can be controlled from the internet.',
 scanning:'Searching for networks…',refresh:'Search again',
 other:'Another network (type the name)…',wifiName:'Wi-Fi name',
 passFor:'Password for',connect:'Connect',
 connecting:'Connecting… the connection may blink for a few seconds — stay on this page.',
 connOk:'Connected!',connFail:'Could not connect. Check the password and try again.',
 skipWifi:'Skip for now',
 skipWarn:'Without Wi-Fi: the clock, the schedules, and internet control will not work until you set it up later from the main page. Skip anyway?',
 skipYes:'Yes, skip',
 gTitle:'Neighborhood generator (optional)',
 gQ:'Is the GENSET_ACTIVE Wi-Fi emitter installed at your generator?',
 gNote:'This is a separate small device installed at the generator. If you don’t have one, choose No.',
 yes:'Yes',no:'No',
 gAction:'When the generator turns on:',
 gOff:'Turn the AC OFF',gEco:'Switch to ECO',
 gDelay:'Delay before sending (seconds)',
 gSeen:'✓ Generator network is visible now',
 gNotSeen:'Generator network is not visible right now (that’s OK if the generator is off).',
 gSave:'Save and continue',
 dTitle:'All set!',
 dOn:'ON button',dOff:'OFF button',dEco:'ECO button',dWifi:'Wi-Fi',dGen:'Generator',
 done:'recorded',skipped:'skipped',connected:'connected',enabled:'enabled',disabled:'off',
 dNote:'Your personal internet control link will appear on the main page once the device connects to the server.',
 dLink:'Your internet control link — save it and open it from anywhere:',
 dWarn:'⚠ Save this link NOW! Bookmark it or write it down. The ERemote Wi-Fi will close in 5 minutes to rest the device — after that you control the AC only through this link.',
 copy:'Copy',copied:'Link copied.',
 open:'Open control panel',
 delayS:['3 seconds','5 seconds','10 seconds','15 seconds','30 seconds','1 minute','2 minutes','5 minutes']},
ar:{
 bn_on:'التشغيل',bn_off:'الإطفاء',bn_eco:'الاقتصادي',
 wTitle:'أهلاً بك في ERemote',wSub:'جهاز التحكم الذكي بالمكيف. الإعداد يستغرق دقيقتين تقريباً. اختر لغتك:',
 recTitle:'تسجيل زر {B}',
 recHow:'أحضر ريموت المكيف. وجّهه نحو النقطة السوداء على الجهاز، ثم اضغط زر {B} مرة واحدة.',
 listening:'جارٍ الاستماع… اضغط الزر الآن',
 recOk:'تم التسجيل بنجاح!',
 recPre:'هذا الزر مسجَّل مسبقاً.',
 recFail:'لم يصل شيء. وجّه الريموت نحو النقطة السوداء وحاول مجدداً.',
 recOvf:'الإشارة طويلة جداً — حاول من مسافة ٣٠ سم تقريباً.',
 test:'جرّبه على المكيف',testSent:'تم الإرسال — هل استجاب المكيف؟',
 rerec:'إعادة التسجيل',retry:'حاول مجدداً',next:'التالي',back:'رجوع',
 skipEco:'الريموت لا يحتوي على زر اقتصادي — تخطَّ',
 skipRec:'تخطَّ هذا الزر — أعدّه لاحقاً',
 fTitle:'الاتصال بواي فاي المنزل',
 fSub:'اختر شبكة الواي فاي في منزلك ليحصل الجهاز على الوقت الصحيح وتتمكن من التحكم به عبر الإنترنت.',
 scanning:'جارٍ البحث عن الشبكات…',refresh:'إعادة البحث',
 other:'شبكة أخرى (اكتب الاسم)…',wifiName:'اسم شبكة الواي فاي',
 passFor:'كلمة مرور',connect:'اتصال',
 connecting:'جارٍ الاتصال… قد ينقطع الاتصال لثوانٍ قليلة — ابقَ في هذه الصفحة.',
 connOk:'تم الاتصال!',connFail:'تعذّر الاتصال. تحقق من كلمة المرور وحاول مجدداً.',
 skipWifi:'تخطَّ الآن',
 skipWarn:'بدون واي فاي: الساعة والجدولة والتحكم عبر الإنترنت لن تعمل حتى تقوم بإعدادها لاحقاً من الصفحة الرئيسية. هل تريد التخطي؟',
 skipYes:'نعم، تخطَّ',
 gTitle:'مولّدة الحي (اختياري)',
 gQ:'هل تم تركيب جهاز بث GENSET_ACTIVE عند المولّدة؟',
 gNote:'هذا جهاز صغير منفصل يُركَّب عند المولّدة. إذا لم يكن لديك واحد، اختر لا.',
 yes:'نعم',no:'لا',
 gAction:'عند تشغيل المولّدة:',
 gOff:'إطفاء المكيف',gEco:'التحويل للوضع الاقتصادي',
 gDelay:'التأخير قبل الإرسال (بالثواني)',
 gSeen:'✓ شبكة المولّدة مرئية الآن',
 gNotSeen:'شبكة المولّدة غير مرئية حالياً (هذا طبيعي إذا كانت المولّدة مطفأة).',
 gSave:'حفظ ومتابعة',
 dTitle:'اكتمل الإعداد!',
 dOn:'زر التشغيل',dOff:'زر الإطفاء',dEco:'الزر الاقتصادي',dWifi:'الواي فاي',dGen:'المولّدة',
 done:'مسجَّل',skipped:'تم تخطيه',connected:'متصل',enabled:'مفعَّل',disabled:'معطَّل',
 dNote:'رابط التحكم عبر الإنترنت الخاص بك سيظهر في الصفحة الرئيسية بعد اتصال الجهاز بالخادم.',
 dLink:'رابط التحكم عبر الإنترنت الخاص بك — احفظه وافتحه من أي مكان:',
 dWarn:'⚠ احفظ هذا الرابط الآن! أضِفه للمفضلة أو اكتبه. ستُغلق شبكة ERemote خلال ٥ دقائق لإراحة الجهاز — بعدها يمكنك التحكم بالمكيف عبر هذا الرابط فقط.',
 copy:'نسخ',copied:'تم نسخ الرابط.',
 open:'فتح لوحة التحكم',
 delayS:['٣ ثوانٍ','٥ ثوانٍ','١٠ ثوانٍ','١٥ ثانية','٣٠ ثانية','دقيقة واحدة','دقيقتان','٥ دقائق']}};
var DELAYS=[3,5,10,15,30,60,120,300];
var L='ar',ST=null,cur='welcome',timers=[];
var wifiPicked='',wifiSkipped=false,ecoSkipped=false,gsChoice=null;

function t(k){return D[L][k]}
function $(id){return document.getElementById(id)}
function h(html){$('scr').innerHTML=html}
function clearTimers(){timers.forEach(clearInterval);timers=[]}
function every(ms,fn){var iv=setInterval(fn,ms);timers.push(iv);return iv}

function setLang(l,rerender){L=l;try{localStorage.setItem('erl',l)}catch(e){}
document.documentElement.lang=l;document.documentElement.dir=(l=='ar')?'rtl':'ltr';
$('langbtn').textContent=(l=='ar')?'EN':'عربي';
if(rerender)go(cur)}

async function getStatus(){try{var r=await fetch('/api/status');ST=await r.json()}catch(e){}}
function codeSet(b){return !!(ST&&ST.codes&&ST.codes[b]&&ST.codes[b].set)}

/* ---------- navigation ---------- */
var STEPS=['on','off','eco','wifi','genset','done'];
function dots(i){var d=$('dots');d.style.display='flex';d.innerHTML='';
for(var k=0;k<STEPS.length;k++){var s=document.createElement('div');
s.className='dot'+(k<i?' done':k==i?' act':'');d.appendChild(s)}}
function go(id){clearTimers();cur=id;
if(id=='welcome')showWelcome();
else if(id=='on'||id=='off'||id=='eco')showRecord(id);
else if(id=='wifi')showWifi();
else if(id=='genset')showGenset();
else if(id=='done')showDone()}
function nextOf(id){return STEPS[STEPS.indexOf(id)+1]}

/* ---------- welcome ---------- */
function showWelcome(){$('dots').style.display='none';$('langbtn').style.display='none';
h('<div class="logo"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M4 6h16v9H4z"/><path d="M8 19h8M12 15v4"/><circle cx="8" cy="10.5" r="1"/><path d="M12 9v3M16 9v3"/></svg></div>'+
'<h1>'+t('wTitle')+'</h1><p>'+t('wSub')+'</p>'+
'<div class="big2"><button onclick="pickLang(\'ar\')">العربية</button>'+
'<button onclick="pickLang(\'en\')">English</button></div>')}
function pickLang(l){setLang(l,false);$('langbtn').style.display='block';go('on')}

/* ---------- record steps ---------- */
function recHdr(b){return '<h1>'+t('recTitle').replace('{B}',t('bn_'+b))+'</h1>'}
function showRecord(b){dots(STEPS.indexOf(b));
if(codeSet(b)){renderRecDone(b,true);return}
startRec(b)}
function startRec(b){
clearTimers();                    // drop any prior record window still polling
h(recHdr(b)+'<p>'+t('recHow').replace('{B}',t('bn_'+b))+'</p>'+
'<div class="pulse">⚫</div><div class="count" id="cnt">30</div>'+
'<div class="st" id="rst">'+t('listening')+'</div>'+
'<button class="ghost" onclick="go(nextOf(\''+b+'\'))">'+t(b=='eco'?'skipEco':'skipRec')+'</button>');
var seq0=(ST&&ST.lastCapture&&ST.lastCapture.seq)||0;   // baseline; wait for a NEW capture
fetch('/api/record?btn='+b,{method:'POST'}).catch(function(){});
var n=0;
every(1000,async function(){n++;
var c=$('cnt');if(c)c.textContent=Math.max(0,30-n);
await getStatus();
var lc=ST&&ST.lastCapture;
if(lc&&lc.seq!==seq0&&lc.btn==b){clearTimers();renderRecDone(b,false);return}
if(n>=30){clearTimers();
var lc=ST&&ST.lastCapture,ovf=lc&&lc.btn==b&&lc.overflow;
h(recHdr(b)+'<div class="okmark" style="background:#3a1d1d;color:var(--bad)">✗</div>'+
'<div class="st bad">'+(ovf?t('recOvf'):t('recFail'))+'</div>'+
'<button class="pri" onclick="startRec(\''+b+'\')">'+t('retry')+'</button>'+
'<button class="ghost" onclick="go(nextOf(\''+b+'\'))">'+t(b=='eco'?'skipEco':'skipRec')+'</button>')}})}
function renderRecDone(b,pre){
var lc=ST&&ST.lastCapture,info=(lc&&lc.btn==b&&lc.proto)?' ('+lc.proto+')':'';
h(recHdr(b)+'<div class="okmark">✓</div>'+
'<div class="st ok">'+(pre?t('recPre'):t('recOk')+info)+'</div>'+
'<button class="pri" onclick="go(nextOf(\''+b+'\'))">'+t('next')+'</button>'+
'<button class="sec" onclick="testBtn(\''+b+'\')">'+t('test')+'</button>'+
'<button class="ghost" onclick="startRec(\''+b+'\')">'+t('rerec')+'</button>')}
async function testBtn(b){try{await fetch('/api/send?btn='+b,{method:'POST'})}catch(e){}
var e=document.querySelector('.st');if(e){e.textContent=t('testSent');e.className='st warn'}}

/* ---------- wifi step ---------- */
function bars(db){return db>=-60?'▂▄▆█':db>=-70?'▂▄▆':db>=-80?'▂▄':'▂'}
function showWifi(){dots(3);
if(ST&&ST.wifi&&ST.wifi.connected){
h('<h1>'+t('fTitle')+'</h1><div class="okmark">✓</div>'+
'<div class="st ok">'+t('connOk')+' ('+(ST.wifi.ssid||'')+')</div>'+
'<button class="pri" onclick="go(\'genset\')">'+t('next')+'</button>'+
'<button class="ghost" onclick="renderWifiList()">'+t('refresh')+'</button>');return}
renderWifiList()}
function renderWifiList(){clearTimers();
h('<h1>'+t('fTitle')+'</h1><p>'+t('fSub')+'</p>'+
'<div class="spin" id="wspin"></div><div class="netlist" id="nets" style="display:none"></div>'+
'<button class="sec" id="wref" style="display:none" onclick="renderWifiList()">'+t('refresh')+'</button>'+
'<button class="ghost" onclick="wifiSkipAsk()">'+t('skipWifi')+'</button>');
var tries=0;
var poll=async function(){tries++;
try{var r=await fetch('/api/scan');var s=await r.json();
if(s.networks&&s.networks.length){clearTimers();
var el=$('nets');el.style.display='block';$('wspin').style.display='none';
$('wref').style.display='block';el.innerHTML='';
s.networks.sort(function(a,b){return b.db-a.db});
s.networks.forEach(function(nw){var d=document.createElement('div');d.className='net';
d.innerHTML='<span></span><span class="m">'+bars(nw.db)+(nw.sec?' 🔒':'')+'</span>';
d.firstChild.textContent=nw.n;
d.onclick=function(){showWifiPass(nw.n,nw.sec)};el.appendChild(d)});
var o=document.createElement('div');o.className='net';
o.innerHTML='<span style="color:var(--mut)">'+t('other')+'</span><span></span>';
o.onclick=function(){showWifiPass('',true)};el.appendChild(o)}
}catch(e){}
if(tries>=12)clearTimers()};
poll();every(2000,poll)}
function showWifiPass(name,sec){clearTimers();
h('<h1>'+t('fTitle')+'</h1>'+
(name?'<p>'+t('passFor')+' <b style="color:var(--txt)">'+name.replace(/</g,'&lt;')+'</b></p>'
:'<label>'+t('wifiName')+'</label><input id="wname" maxlength="32">')+
(sec?'<input id="wpass" type="password" maxlength="64" placeholder="••••••••">':'')+
'<button class="pri" onclick="wifiConnect(\''+name.replace(/'/g,"\\'")+'\','+sec+')">'+t('connect')+'</button>'+
'<button class="ghost" onclick="renderWifiList()">'+t('back')+'</button>')}
async function wifiConnect(name,sec){
var n=name||($('wname')?$('wname').value.trim():'');if(!n)return;
var p=sec&&$('wpass')?$('wpass').value:'';
wifiPicked=n;
h('<h1>'+t('fTitle')+'</h1><div class="spin"></div>'+
'<div class="st warn">'+t('connecting')+'</div>');
try{await fetch('/api/wifi',{method:'POST',body:JSON.stringify({ssid:n,pass:p})})}catch(e){}
var n2=0;
every(2000,async function(){n2++;await getStatus();
if(ST&&ST.wifi&&ST.wifi.connected){clearTimers();showWifi();return}
if(n2>=13){clearTimers();
h('<h1>'+t('fTitle')+'</h1><div class="okmark" style="background:#3a1d1d;color:var(--bad)">✗</div>'+
'<div class="st bad">'+t('connFail')+'</div>'+
'<button class="pri" onclick="showWifiPass(\''+name.replace(/'/g,"\\'")+'\','+sec+')">'+t('retry')+'</button>'+
'<button class="ghost" onclick="renderWifiList()">'+t('back')+'</button>')}})}
function wifiSkipAsk(){clearTimers();
h('<h1>'+t('fTitle')+'</h1><p style="color:var(--warn)">'+t('skipWarn')+'</p>'+
'<button class="sec" onclick="wifiSkipped=true;go(\'genset\')">'+t('skipYes')+'</button>'+
'<button class="pri" onclick="renderWifiList()">'+t('back')+'</button>')}

/* ---------- genset step ---------- */
function showGenset(){dots(4);
h('<h1>'+t('gTitle')+'</h1><p>'+t('gQ')+'</p><p style="font-size:14px">'+t('gNote')+'</p>'+
'<div class="big2"><button onclick="gensetYes()">'+t('yes')+'</button>'+
'<button class="sec" style="margin:0" onclick="gensetNo()">'+t('no')+'</button></div>')}
async function gensetNo(){gsChoice='disabled';
try{await fetch('/api/genset',{method:'POST',body:JSON.stringify(
{mode:'disabled',delay:3,ssid:'GENSET_ACTIVE'})})}catch(e){}
go('done')}
function gensetYes(){clearTimers();
h('<h1>'+t('gTitle')+'</h1><p>'+t('gAction')+'</p>'+
'<div class="radio"><input type="radio" name="ga" id="ga_off" checked>'+
'<label for="ga_off" style="font-size:15px;color:var(--txt)">'+t('gOff')+'</label></div>'+
'<div class="radio"><input type="radio" name="ga" id="ga_eco">'+
'<label for="ga_eco" style="font-size:15px;color:var(--txt)">'+t('gEco')+'</label></div>'+
'<label>'+t('gDelay')+'</label>'+
'<input id="gdel" type="number" min="0" max="3600" value="3" inputmode="numeric">'+
'<div class="st" id="gseen"></div>'+
'<button class="pri" onclick="gensetSave()">'+t('gSave')+'</button>'+
'<button class="ghost" onclick="showGenset()">'+t('back')+'</button>');
var chk=async function(){await getStatus();var e=$('gseen');if(!e)return;
var seen=ST&&ST.genset&&ST.genset.detected;
e.textContent=seen?t('gSeen'):t('gNotSeen');
e.className='st '+(seen?'ok':'')};
chk();every(3000,chk)}
async function gensetSave(){
gsChoice=$('ga_eco').checked?'eco':'off';
try{await fetch('/api/genset',{method:'POST',body:JSON.stringify(
{mode:gsChoice,delay:Math.max(0,parseInt($('gdel').value)||0),ssid:'GENSET_ACTIVE'})})}catch(e){}
go('done')}

/* ---------- done ---------- */
async function showDone(){dots(5);
try{await fetch('/api/init',{method:'POST'})}catch(e){}
await getStatus();
renderDoneCard();
// The claim can take a few more seconds after Wi-Fi came up; keep polling
// and swap the generic note for the real link the moment it exists.
if(!(ST&&ST.remote&&ST.remote.claimed)){var n=0;
every(3000,async function(){n++;await getStatus();
if(ST&&ST.remote&&ST.remote.claimed){clearTimers();renderDoneCard()}
else if(n>=30)clearTimers()})}}
function renderDoneCard(){
var row=function(name,ok,okTxt,noTxt){
return name+': <b'+(ok?'':' style="color:var(--mut)"')+'>'+(ok?okTxt:noTxt)+'</b><br>'};
var r=(ST&&ST.remote)||{};
var rlink=r.code?('https://er.my.to/r/'+r.code):'';   // always show the domain link
var linkPart=(r.claimed&&rlink)
?'<p style="margin-bottom:6px">'+t('dLink')+'</p>'+
 '<div class="linkrow"><input id="wlink" readonly value="'+rlink+'">'+
 '<button onclick="copyWLink()">'+t('copy')+'</button></div>'+
 '<p style="color:var(--bad);font-weight:800;font-size:15.5px;line-height:1.7;'+
 'background:rgba(248,113,113,.1);border:1px solid rgba(248,113,113,.4);'+
 'border-radius:12px;padding:12px;margin:4px 0 6px">'+t('dWarn')+'</p>'
:'<p>'+t('dNote')+'</p>';
h('<h1>'+t('dTitle')+'</h1><div class="okmark">✓</div><p class="sum" style="text-align:center">'+
row(t('dOn'),codeSet('on'),t('done'),t('skipped'))+
row(t('dOff'),codeSet('off'),t('done'),t('skipped'))+
row(t('dEco'),codeSet('eco'),t('done'),t('skipped'))+
row(t('dWifi'),ST&&ST.wifi&&ST.wifi.connected,t('connected'),t('skipped'))+
row(t('dGen'),gsChoice&&gsChoice!='disabled',t('enabled'),t('disabled'))+
'</p>'+linkPart+
'<button class="pri" onclick="location.href=\'/\'">'+t('open')+'</button>')}
function copyWLink(){var i=$('wlink');i.select();i.setSelectionRange(0,200);
try{navigator.clipboard.writeText(i.value).then(function(){},function(){document.execCommand('copy')})}
catch(e){document.execCommand('copy')}
var b=document.querySelector('.linkrow button');if(b)b.textContent=t('copied')}

/* ---------- boot ---------- */
(async function(){
var s='ar';try{s=localStorage.getItem('erl')||'ar'}catch(e){}
setLang(s,false);
await getStatus();
go('welcome')})();
</script></body></html>
)WIZ";
