#pragma once
// Dashboard page served at "/". Self-contained (no CDN) so it works in AP mode.
#include <Arduino.h>

static const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Water Level Controller</title>
<style>
:root{--bg:#0f1720;--card:#18222e;--line:#263342;--fg:#e6edf3;--mut:#8b9bb0;--acc:#38bdf8;--ok:#22c55e;--warn:#f59e0b;--bad:#ef4444}
@media (prefers-color-scheme:light){:root{--bg:#f3f6f9;--card:#fff;--line:#dde4ea;--fg:#14202c;--mut:#5f6f80}}
*{box-sizing:border-box}body{margin:0;font:15px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--fg)}
header{display:flex;justify-content:space-between;align-items:center;padding:12px 16px;border-bottom:1px solid var(--line)}
header b{font-size:17px}header small{color:var(--mut)}
main{max-width:980px;margin:0 auto;padding:14px;display:grid;gap:14px;grid-template-columns:repeat(auto-fit,minmax(290px,1fr))}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px}
.card h3{margin:0 0 10px;font-size:13px;text-transform:uppercase;letter-spacing:.06em;color:var(--mut)}
.wide{grid-column:1/-1}
.tankwrap{display:flex;gap:16px;align-items:center}
.tank{position:relative;width:96px;height:180px;border:3px solid var(--mut);border-radius:10px;overflow:hidden;flex:none}
.water{position:absolute;left:0;right:0;bottom:0;background:linear-gradient(#38bdf8,#0284c7);transition:height .8s}
.mark{position:absolute;left:0;right:0;border-top:2px dashed var(--warn);opacity:.8}
.mark.stop{border-color:var(--ok)}
.big{font-size:44px;font-weight:700;line-height:1}.mut{color:var(--mut)}
.pill{display:inline-block;padding:3px 10px;border-radius:99px;font-weight:600;font-size:13px;background:var(--line)}
.on{background:var(--ok);color:#04130a}.flt{background:var(--bad);color:#fff}.hold{background:var(--warn);color:#1c1300}
.bar{position:relative;height:10px;border-radius:5px;background:var(--line);margin:10px 0 4px;overflow:hidden}.bar div{height:100%;width:0;background:var(--acc);transition:width .5s}.bar i{position:absolute;top:0;bottom:0;width:2px}#aunder{background:var(--warn)}#aover{background:var(--bad)}
.grid2{display:grid;grid-template-columns:1fr 1fr;gap:8px 14px}.grid2 div span{display:block;font-size:12px;color:var(--mut)}.grid2 div b{font-size:18px}
.btns{display:flex;flex-wrap:wrap;gap:8px;margin-top:10px}
button{font:inherit;padding:9px 14px;border-radius:8px;border:1px solid var(--line);background:var(--line);color:var(--fg);cursor:pointer}
button.p{background:var(--acc);border-color:var(--acc);color:#04202e;font-weight:600}button.d{background:var(--bad);border-color:var(--bad);color:#fff}
.seg{display:flex;border:1px solid var(--line);border-radius:8px;overflow:hidden}.seg button{border:0;border-radius:0;flex:1}.seg button.sel{background:var(--acc);color:#04202e;font-weight:600}
#faultbox{display:none;margin-top:10px;padding:10px;border-radius:8px;background:rgba(239,68,68,.15);border:1px solid var(--bad)}
ul.ev{list-style:none;margin:0;padding:0;max-height:260px;overflow:auto;font-size:13px}ul.ev li{padding:4px 0;border-bottom:1px solid var(--line)}ul.ev time{color:var(--mut);margin-right:8px;font-variant-numeric:tabular-nums}
details{border-top:1px solid var(--line);padding:8px 0}summary{cursor:pointer;font-weight:600}
.f{display:grid;grid-template-columns:1fr 140px;gap:6px 10px;align-items:center;margin:10px 0}
.f label{font-size:14px}.f input[type=text],.f input[type=password],.f input[type=number]{width:100%;padding:6px 8px;border-radius:6px;border:1px solid var(--line);background:var(--bg);color:var(--fg);font:inherit}
.f input[type=checkbox]{justify-self:start;width:20px;height:20px}
#toast{position:fixed;left:50%;bottom:18px;transform:translateX(-50%);background:var(--fg);color:var(--bg);padding:8px 14px;border-radius:8px;display:none}
</style></head><body>
<header><div><b>Smart Water Level Controller</b><br><small id="net">-</small></div><small id="clock"></small></header>
<main>
 <section class="card">
  <h3>Tank</h3>
  <div class="tankwrap">
   <div class="tank"><div class="water" id="water" style="height:0"></div><div class="mark" id="mStart"></div><div class="mark stop" id="mStop"></div></div>
   <div><div class="big" id="pct">--%</div><div id="litres" class="mut">-- L</div><div class="mut" id="dist" style="margin-top:8px;font-size:13px"></div><div id="sump" class="mut" style="font-size:13px"></div></div>
  </div>
 </section>
 <section class="card">
  <h3>Pump</h3>
  <div><span class="pill" id="pstate">-</span> <span class="mut" id="phold"></span></div>
  <div id="faultbox"><b id="fname"></b><div id="fdetail" class="mut" style="font-size:13px"></div></div>
  <div class="seg" style="margin-top:12px"><button data-m="auto">AUTO</button><button data-m="manual">MANUAL</button><button data-m="off">OFF</button></div>
  <div class="btns"><button class="p" onclick="cmd('start')">Start (fill)</button><button onclick="cmd('stop')">Stop</button><button class="d" onclick="cmd('reset')">Reset fault</button></div>
  <div class="mut" style="font-size:12px;margin-top:8px">AUTO refills at the start level. Stop cancels the current fill; use OFF to keep the pump off.</div>
 </section>
 <section class="card">
  <h3>Pump current</h3>
  <div class="big" id="a" style="font-size:36px">-</div>
  <div class="bar"><div id="abar"></div><i id="aunder"></i><i id="aover"></i></div>
  <div class="mut" style="font-size:12px" id="alim"></div>
  <div class="grid2" style="margin-top:10px">
   <div><span>Mains voltage</span><b id="v">-</b></div><div><span>CT bias / signal</span><b id="ctb" style="font-size:15px">-</b></div>
   <div><span>Starts (since boot)</span><b id="starts">-</b></div><div><span>Run time (since boot)</span><b id="trun">-</b></div>
  </div>
 </section>
 <section class="card wide">
  <h3>Events</h3><ul class="ev" id="ev"></ul>
 </section>
 <section class="card wide">
  <h3>Settings</h3><div id="settings"></div>
  <div class="btns"><button class="p" onclick="saveCfg()">Save settings</button></div>
  <details><summary>Calibration</summary>
   <p class="mut" style="font-size:13px">Fill or drain the tank to the level you want as 0% / 100%, then press the matching button. The current distance reading is stored.</p>
   <div class="btns"><button onclick="cmd('cal_empty',1)">Set current reading as EMPTY</button><button onclick="cmd('cal_full',1)">Set current reading as FULL</button></div>
   <p class="mut" style="font-size:13px"><b>Current transformer:</b> 1) with the pump OFF press <i>Zero CT</i>. 2) Run the pump, measure the same wire with a clamp meter, type the reading and press <i>Calibrate CT</i>.</p>
   <div class="btns"><button onclick="cmd('cal_ct_zero',1)">Zero CT (pump off)</button><input id="calA" type="number" step="0.01" placeholder="clamp meter A" style="width:130px;padding:6px;border-radius:6px;border:1px solid var(--line);background:var(--bg);color:var(--fg)"><button onclick="calv('cal_ct','calA')">Calibrate CT</button></div>
   <p class="mut" style="font-size:13px"><b>Voltage sensor</b> (if fitted): type the mains voltage from a meter.</p>
   <div class="btns"><input id="calV" type="number" step="0.1" placeholder="meter V" style="width:130px;padding:6px;border-radius:6px;border:1px solid var(--line);background:var(--bg);color:var(--fg)"><button onclick="calv('cal_volt','calV')">Calibrate voltage</button></div>
  </details>
  <details><summary>System</summary>
   <div class="btns"><button onclick="cmd('restart',1)">Restart</button><button class="d" onclick="cmd('factory_reset',1)">Factory reset</button><button onclick="logout()">Forget password</button></div>
   <p class="mut" style="font-size:13px">Firmware update (.pio/build/esp32dev/firmware.bin):</p>
   <input type="file" id="fw" accept=".bin"> <button onclick="ota()">Upload</button> <span id="otap" class="mut"></span>
   <p class="mut" style="font-size:12px" id="fwv"></p>
  </details>
 </section>
</main>
<div id="toast"></div>
<script>
const $=id=>document.getElementById(id);
let PW=null;try{PW=localStorage.getItem('wlcpw')}catch(e){}
function toast(t){const e=$('toast');e.textContent=t;e.style.display='block';clearTimeout(e._t);e._t=setTimeout(()=>e.style.display='none',3500)}
function hdr(){return PW?{'Authorization':'Basic '+btoa('admin:'+PW)}:{}}
async function api(method,url,body,isForm){
 for(let i=0;i<2;i++){
  const h=hdr();if(body&&!isForm)h['Content-Type']='application/json';
  const r=await fetch(url,{method,headers:h,body:isForm?body:(body?JSON.stringify(body):undefined)});
  if(r.status!==401)return r.json().catch(()=>({}));
  const p=prompt('Admin password');if(p===null)throw new Error('cancelled');
  PW=p;try{localStorage.setItem('wlcpw',p)}catch(e){}
 }
 throw new Error('unauthorized');
}
function logout(){PW=null;try{localStorage.removeItem('wlcpw')}catch(e){};toast('Password forgotten')}
const CONFIRM={factory_reset:'Erase ALL settings (incl. Wi-Fi) and restart?',restart:'Restart the controller?'};
async function cmd(c,quiet){
 if(CONFIRM[c]&&!confirm(CONFIRM[c]))return;
 try{const r=await api('POST','/api/cmd?cmd='+c);toast(r.msg||(r.ok?'ok':'failed'));if(c.startsWith('cal'))loadCfg();poll()}catch(e){toast(e.message)}
}
async function calv(c,id){const v=$(id).value;if(!v)return toast('Enter a value first');try{const r=await api('POST','/api/cmd?cmd='+c+'&value='+encodeURIComponent(v));toast(r.msg);loadCfg()}catch(e){toast(e.message)}}
document.querySelectorAll('.seg button').forEach(b=>b.onclick=()=>cmd(b.dataset.m));
const hms=s=>{s=s|0;const h=s/3600|0,m=(s%3600)/60|0,x=s%60;return(h?h+'h ':'')+(h||m?m+'m ':'')+x+'s'};
const fmt=(v,u)=>v===undefined?'-':v+' '+u;
async function poll(){
 try{
  const s=await (await fetch('/api/status')).json();
  const L=s.level,E=s.elec,U=s.pump;
  $('net').textContent=(s.ap?'Setup AP · ':'')+s.ip+(s.rssi?' · '+s.rssi+' dBm':'')+' · '+s.host+'.local';
  $('clock').textContent=s.time?new Date(s.time*1000).toLocaleString():'up '+hms(s.uptime);
  const pct=L.ok&&L.pct!==undefined?L.pct:null;
  $('water').style.height=(pct??0)+'%';
  $('pct').textContent=pct===null?'--%':Math.round(pct)+'%';
  $('litres').textContent=pct===null?'sensor offline':Math.round(L.litres)+' L of '+Math.round(L.capacity)+' L';
  $('dist').textContent='Distance '+(L.distCm??'-')+' cm · raw '+L.raw+' · errors '+L.err_n;
  $('mStart').style.bottom=L.start+'%';$('mStop').style.bottom=L.stop+'%';
  $('sump').textContent=s.sump.enabled?('Sump: '+(s.sump.water?'water OK':'EMPTY')):'';
  const ps=$('pstate');
  if(U.fault){ps.textContent='FAULT';ps.className='pill flt'}
  else if(U.on){ps.textContent='RUNNING '+hms(U.runS);ps.className='pill on'}
  else{ps.textContent='STOPPED';ps.className='pill'+(U.fill?' hold':'')}
  $('phold').textContent=U.fault||U.on?'':(U.hold+(U.minOff?' ('+U.minOff+' s)':''));
  $('faultbox').style.display=U.fault?'block':'none';
  $('fname').textContent=U.faultName+(U.latched?' — locked, press Reset':'');
  $('fdetail').textContent=U.retryIn?('Auto retry in '+hms(U.retryIn)+' · attempt '+U.retries):(U.latched?'':'Clears automatically when the condition is gone');
  async function calv(c,id){const v=$(id).value;if(!v)return toast('Enter a value first');try{const r=await api('POST','/api/cmd?cmd='+c+'&value='+encodeURIComponent(v));toast(r.msg);loadCfg()}catch(e){toast(e.message)}}
document.querySelectorAll('.seg button').forEach(b=>b.classList.toggle('sel',b.dataset.m.toUpperCase()===U.mode));
  $('a').textContent=E.ok?fmt(E.a,'A'):'CT fault';
  const full=Math.max(E.overA*1.25,1);$('abar').style.width=Math.min(100,(E.a||0)/full*100)+'%';
  $('aunder').style.left=(E.underA/full*100)+'%';$('aover').style.left=(E.overA/full*100)+'%';
  $('alim').textContent='underload < '+E.underA+' A · overload > '+E.overA+' A';
  $('v').textContent=E.vEnabled?(E.vOk?fmt(E.v,'V'):'sensor fault'):'not fitted';
  $('ctb').textContent=(E.ctBiasV??'-')+' V / '+(E.ctRmsV!==undefined?(E.ctRmsV*1000).toFixed(1)+' mV':'-');
  $('starts').textContent=U.starts;$('trun').textContent=hms(U.totalRunS);
  $('fwv').textContent='Firmware v'+s.fw;
 }catch(e){$('net').textContent='connection lost…'}
}
async function events(){
 try{const a=await (await fetch('/api/events')).json();
  $('ev').innerHTML=a.map(e=>'<li><time>'+(e.t?new Date(e.t*1000).toLocaleString():'+'+hms(e.up))+'</time>'+e.msg.replace(/</g,'&lt;')+'</li>').join('')}catch(e){}
}
// [key, label, type]  type: n=number(default) b=bool s=text p=password h=hex
const S=[
 ['Pump control',[['ctl.startLevelPct','Start filling at (%)'],['ctl.stopLevelPct','Stop at (%)'],['ctl.minOffS','Min. off time / power-up delay (s)'],['ctl.maxRunMin','Max run time (min, 0 = off)']]],
 ['Overload',[['ctl.overloadEnabled','Enabled','b'],['ctl.overloadCurrentA','Trip above current (A)'],['ctl.overloadDelayS','Trip delay (s)']]],
 ['Dry run / underload',[['ctl.dryRunEnabled','Enabled','b'],['ctl.underloadCurrentA','Trip below current (A)'],['ctl.dryRunDelayS','Trip delay (s)'],['ctl.startGraceS','Priming grace after start (s)'],['ctl.noRiseEnabled','Level-not-rising check','b'],['ctl.noRiseWindowMin','Check window (min)'],['ctl.noRiseMinPct','Min. rise per window (%)'],['ctl.retryDelayMin','Auto-retry after (min)'],['ctl.maxRetries','Max retries (0 = lock at once)'],['ctl.sumpFloatEnabled','Sump float switch fitted','b']]],
 ['Current transformer',[['ct.ampsPerVolt','Amps per ADC volt RMS (SCT-013-030 = 30)'],['ct.zeroA','Zero offset (A)'],['ct.minA','Show as 0 below (A)'],['ct.mainsHz','Mains frequency (Hz)'],['ctl.requireCurrentSensor','Stop pump if CT input invalid','b']]],
 ['Voltage (ZMPT101B, optional)',[['ctl.voltageEnabled','Sensor fitted - enable protection','b'],['ct.voltsPerVolt','Volts per ADC volt RMS'],['ctl.overVoltageV','Over voltage (V)'],['ctl.underVoltageV','Under voltage (V)'],['ctl.voltageHystV','Hysteresis (V)'],['ctl.voltageTripS','Trip delay (s)'],['ctl.voltageRestoreS','Restore delay (s)']]],
 ['Tank',[['tank.emptyDistCm','Sensor distance when EMPTY (cm)'],['tank.fullDistCm','Sensor distance when FULL (cm)'],['tank.capacityL','Usable capacity (L)']]],
 ['Level sensor (RS485 Modbus)',[['sensor.address','Slave address'],['sensor.baud','Baud rate'],['sensor.function','Function (3 = holding, 4 = input)'],['sensor.reg','Distance register','h'],['sensor.unitMm','mm per count (1 = mm, 10 = cm)'],['sensor.median','Median filter samples (1-9)'],['ctl.sensorTimeoutS','Sensor timeout (s)']]],
 ['Network & security',[['net.ssid','Wi-Fi SSID','s'],['net.pass','Wi-Fi password (blank = keep)','p'],['net.hostname','Hostname','s'],['net.tz','Time zone (POSIX, e.g. IST-5:30)','s'],['net.adminPass','New admin password (blank = keep)','p']]]
];
let CFG=null;
const get=(o,k)=>k.split('.').reduce((a,p)=>a?.[p],o);
function render(){
 $('settings').innerHTML=S.map(([t,fs],i)=>'<details'+(i==0?' open':'')+'><summary>'+t+'</summary><div class="f">'+fs.map(([k,l,ty])=>{
  const v=get(CFG,k),id='c_'+k.replace('.','_');
  if(ty==='b')return'<label for="'+id+'">'+l+'</label><input type="checkbox" id="'+id+'"'+(v?' checked':'')+'>';
  if(ty==='p')return'<label for="'+id+'">'+l+'</label><input type="password" id="'+id+'" autocomplete="new-password">';
  if(ty==='s')return'<label for="'+id+'">'+l+'</label><input type="text" id="'+id+'" value="'+String(v??'').replace(/"/g,'&quot;')+'">';
  if(ty==='h')return'<label for="'+id+'">'+l+'</label><input type="text" id="'+id+'" value="0x'+(v|0).toString(16).padStart(4,'0')+'">';
  return'<label for="'+id+'">'+l+'</label><input type="number" step="any" id="'+id+'" value="'+v+'">';
 }).join('')+'</div></details>').join('');
}
async function loadCfg(){try{CFG=await (await fetch('/api/config')).json();render()}catch(e){toast('Could not load settings')}}
async function saveCfg(){
 const out={};
 S.forEach(([,fs])=>fs.forEach(([k,,ty])=>{
  const el=$('c_'+k.replace('.','_'));let v;
  if(ty==='b')v=el.checked;else if(ty==='s')v=el.value.trim();else if(ty==='p'){if(!el.value)return;v=el.value}
  else{v=Number(el.value);if(!isFinite(v))return}
  const [g,f]=k.split('.');(out[g]=out[g]||{})[f]=v;
 }));
 try{const r=await api('POST','/api/config',out);toast(r.restart?'Saved — restarting…':'Saved');loadCfg()}catch(e){toast(e.message)}
}
async function ota(){
 const f=$('fw').files[0];if(!f)return toast('Choose firmware.bin first');
 if(!confirm('Upload '+f.name+'? The pump will stop while the controller restarts.'))return;
 const fd=new FormData();fd.append('firmware',f,f.name);$('otap').textContent='uploading…';
 try{const r=await api('POST','/api/update',fd,true);$('otap').textContent=r.msg||'';}catch(e){$('otap').textContent=e.message}
}
poll();events();loadCfg();setInterval(poll,1000);setInterval(events,5000);
</script></body></html>)rawliteral";
