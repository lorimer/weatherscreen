// The single-page web management interface. Served from flash so OTA updates it too.
// Self-contained (no CDN): charts are drawn as inline SVG by the script below.
#pragma once

static const char WEB_PAGE[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Old Man Weather</title>
<style>
:root{--bg:#F6F3EC;--card:#fff;--ink:#1C1B19;--mut:#5A564E;--rule:#DDD6C8;--blue:#2B5F9E;--red:#B3261E;--org:#A84B0C;--grn:#2F7D4F}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.45 system-ui,-apple-system,Segoe UI,sans-serif}
header{display:flex;flex-wrap:wrap;gap:8px 24px;align-items:baseline;padding:16px 24px;border-bottom:2px solid var(--rule)}
h1{font-size:22px;margin:0}#sub{color:var(--mut)}
nav{display:flex;gap:4px;padding:0 24px;border-bottom:2px solid var(--rule);flex-wrap:wrap}
nav button{background:none;border:0;padding:12px 14px;font:inherit;color:var(--mut);cursor:pointer;border-bottom:3px solid transparent;margin-bottom:-2px}
nav button.on{color:var(--ink);border-color:var(--blue);font-weight:600}
main{padding:20px 24px;max-width:1180px}section{display:none}section.on{display:block}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:16px}
.card{background:var(--card);border:1px solid var(--rule);border-radius:10px;padding:16px 18px;margin-bottom:16px}
.card h2{font-size:15px;text-transform:uppercase;letter-spacing:.05em;color:var(--mut);margin:0 0 10px}
.card h3{font-size:14px;color:var(--mut);margin:14px 0 2px;font-weight:600}
.big{font-size:40px;font-weight:700;line-height:1.1}
table{border-collapse:collapse;width:100%}td,th{text-align:left;padding:6px 8px;border-bottom:1px solid var(--rule);font-variant-numeric:tabular-nums}
th{color:var(--mut);font-weight:600;font-size:14px}.kv td:first-child{color:var(--mut);width:42%}
label{display:block;margin:10px 0 4px;color:var(--mut);font-size:14px}
input,select{font:inherit;padding:8px 10px;border:1px solid #bdb5a5;border-radius:8px;width:100%;background:#fff}
.row{display:flex;gap:10px;align-items:end;flex-wrap:wrap}.row>*{flex:1}
button.b{font:inherit;padding:9px 16px;border-radius:8px;border:0;background:var(--blue);color:#fff;cursor:pointer;margin-top:12px}
button.alt{background:#E6E0D3;color:var(--ink)}a{color:var(--blue)}
svg{width:100%;height:auto;display:block;user-select:none;-webkit-user-select:none}.note{color:var(--mut);font-size:14px}.ok{color:var(--grn)}.bad{color:var(--org)}
.pill{display:inline-block;padding:2px 8px;border-radius:99px;background:#E6E0D3;font-size:13px}
.bar{position:sticky;top:0;z-index:5;background:var(--bg);padding:10px 0;display:flex;flex-wrap:wrap;gap:8px;align-items:center;border-bottom:1px solid var(--rule);margin-bottom:14px}
.seg{display:inline-flex;border:1px solid #bdb5a5;border-radius:8px;overflow:hidden}.seg button{font:inherit;font-size:14px;border:0;background:#fff;padding:7px 12px;cursor:pointer;border-right:1px solid var(--rule)}
.seg button:last-child{border-right:0}.seg button.on{background:var(--blue);color:#fff}
.bar input[type=date]{width:auto;padding:6px 8px;font-size:14px}.bar .small{font-size:14px;padding:7px 12px;margin:0}
#rlabel{font-weight:600;margin-left:6px}
.tiles{display:grid;grid-template-columns:repeat(auto-fit,minmax(165px,1fr));gap:12px}
.tile{border:1px solid var(--rule);border-radius:8px;padding:10px 12px}.tile .v{font-size:26px;font-weight:700}.tile .k{color:var(--mut);font-size:13px}
.chartbox{position:relative}.tip{position:absolute;pointer-events:none;background:#1C1B19;color:#fff;font-size:13px;padding:6px 9px;border-radius:6px;white-space:nowrap;display:none;z-index:4}
.legend{display:flex;gap:16px;flex-wrap:wrap;font-size:13px;color:var(--mut);margin-top:4px}.legend i{display:inline-block;width:14px;height:4px;border-radius:2px;vertical-align:middle;margin-right:5px}
.busy{opacity:.5;transition:opacity .2s}
</style></head><body>
<header><h1 id="name">Old Man Weather</h1><span id="sub"></span></header>
<nav><button data-t="ov" class="on">Overview</button><button data-t="trends">Trends</button><button data-t="se">Settings</button><button data-t="da">Data &amp; updates</button></nav>
<main>
<section id="ov" class="on"><div class="grid">
 <div class="card"><h2>Now</h2><div class="big" id="now">--</div><div id="cond" class="note"></div>
  <table class="kv" style="margin-top:10px"><tr><td>Pressure</td><td id="pr">--</td></tr><tr><td>Trend (3 h)</td><td id="tr">--</td></tr></table></div>
 <div class="card"><h2>Device</h2><table class="kv" id="dev"></table></div>
</div></section>

<section id="trends">
 <div class="bar">
  <div class="seg" id="presets"><button data-d="2">2 days</button><button data-d="7">Week</button><button data-d="30">Month</button><button data-d="91">3 months</button><button data-d="365">Year</button><button data-d="all">All</button></div>
  <input type="date" id="dfrom" aria-label="From date"> <span class="note">to</span> <input type="date" id="dto" aria-label="To date">
  <button class="b alt small" id="dgo">Show</button>
  <button class="b alt small" id="back" title="Previous range">&larr;</button><button class="b alt small" id="fwd" title="Next range">&rarr;</button>
  <span id="rlabel"></span>
 </div>
 <p class="note" style="margin-top:0">Drag across a chart to zoom in; double-click to zoom out. Hover (or tap) for values.</p>

 <div class="card" id="cardTime"><h2>Pressure and pain over time</h2>
  <h3>Pressure (inHg)</h3><div class="chartbox" id="cP"></div>
  <div class="legend" id="legP"></div>
  <h3>Pain (1 = least, 5 = worst)</h3><div class="chartbox" id="cN"></div>
  <div class="legend" id="legN"></div>
  <div id="fallWrap"><h3>Largest 24-hour pressure fall each day (inHg)</h3><div class="chartbox" id="cF"></div></div>
 </div>

 <div class="card"><h2>Summary for this range</h2><div class="tiles" id="tiles"></div>
  <div class="row" style="max-width:520px;margin-top:6px"><div><label>Count a 24 h fall as "big" from (inHg)</label><input id="bigfall" type="number" step="0.01" min="0.02" value="0.10"></div></div>
 </div>

 <div class="grid">
  <div class="card"><h2>Pain vs. pressure fall (one dot per day)</h2>
   <div class="row" style="max-width:360px"><div><label>Compare pain with the fall on</label><select id="scLag"><option value="0">the same day</option><option value="1">the day before</option></select></div></div>
   <div class="chartbox" id="cS"></div><p class="note" id="scNote"></p></div>
  <div class="card"><h2>How long after a pressure change does pain follow?</h2>
   <div class="chartbox" id="cL"></div><div class="legend" id="legL"></div><p class="note" id="lagNote"></p></div>
 </div>

 <div class="grid">
  <div class="card"><h2>Pain by time of day</h2><div class="chartbox" id="cH"></div>
   <p class="note">Bars: average score in each hour (likely mis-taps count less). Grey: number of taps.</p></div>
  <div class="card"><h2>Month by month</h2><div style="max-height:360px;overflow:auto"><table id="months"></table></div></div>
 </div>

 <div class="card"><h2>Daily values</h2>
  <p class="note"><a id="dlRange" href="#">Download these days as CSV</a></p>
  <div style="max-height:420px;overflow:auto"><table id="daily"></table></div></div>
</section>

<section id="se"><div class="grid">
 <form class="card" id="f1"><h2>Device &amp; location</h2>
  <label>Device name (also the network name)</label><input name="device_name">
  <label>Location</label><div class="row"><input id="q" placeholder="City or ZIP code"><button type="button" class="b alt" id="qs" style="flex:0 0 auto">Search</button></div>
  <select id="qr" style="display:none;margin-top:8px"></select>
  <div class="note" id="loc"></div>
  <input type="hidden" name="place"><input type="hidden" name="lat"><input type="hidden" name="lon"><input type="hidden" name="elevation_m">
  <button class="b">Save</button></form>
 <form class="card" id="f2"><h2>Screen brightness</h2>
  <div class="row"><div><label>Daytime %</label><input name="day_brightness" type="number" min="10" max="100"></div>
  <div><label>Night %</label><input name="night_brightness" type="number" min="3" max="100"></div>
  <div><label>Fade (minutes)</label><input name="fade_minutes" type="number" min="0" max="180"></div></div>
  <p class="note">Dims around sunset and brightens around sunrise. Colors never change.</p><button class="b">Save</button></form>
 <form class="card" id="f3"><h2>Wi-Fi</h2>
  <label>Network name</label><input name="ssid"><label>Password (leave blank to keep)</label><input name="wifi_pass" type="password" autocomplete="new-password">
  <p class="note">The station reconnects right away. If the new details are wrong it opens its own hotspot after 30 minutes.</p><button class="b">Save</button></form>
 <form class="card" id="f4"><h2>Maintenance password</h2>
  <label>New password (8+ characters)</label><input name="admin_pass" type="password" autocomplete="new-password">
  <p class="note">Used for this page (user <b>admin</b>), remote updates and the fallback hotspot.</p><button class="b">Save</button></form>
 <form class="card" id="f5"><h2>Pain analysis (advanced)</h2>
  <div class="row"><div><label>Trend window (h)</label><input name="bandwidth_h" type="number" step="0.5"></div><div><label>Break line after (h)</label><input name="gap_h" type="number" step="0.5"></div></div>
  <div class="row"><div><label>Burst window (h)</label><input name="density_window_h" type="number" step="0.25"></div><div><label>Neighbours for full outlier check</label><input name="support_full" type="number" step="0.5"></div></div>
  <label>Never discount taps within this many points of the trend</label><input name="cutoff_floor" type="number" step="0.25">
  <p class="note">Raw taps are never changed; these only affect how they are weighed. Defaults: 12, 18, 1.5, 2, 2.5.</p><button class="b">Save</button></form>
</div></section>

<section id="da"><div class="grid">
 <div class="card"><h2>Download</h2><p><a href="/export/pain.csv">pain.csv</a> &ndash; every tap (time, score)</p>
  <p><a href="/export/pressure.csv">pressure.csv</a> &ndash; logged pressure</p><p><a href="/export/daily.csv">daily.csv</a> &ndash; calculated daily values, whole history</p>
  <p class="note">Times are Unix seconds (UTC). tools/analyze.py reproduces the analysis on a computer.</p></div>
 <form class="card" id="up" method="POST" action="/update" enctype="multipart/form-data"><h2>Firmware update</h2>
  <input type="file" name="fw" accept=".bin"><button class="b">Upload &amp; install</button>
  <p class="note">Use firmware.bin from <code>.pio/build/station/</code>. The station restarts and rolls back automatically if the new version fails to start.</p><div id="upm"></div></form>
 <div class="card"><h2>Restart</h2><button class="b alt" id="rb">Restart station</button></div>
</div></section>
</main>
<script>
const $=s=>document.querySelector(s),$$=s=>document.querySelectorAll(s);
const j=u=>fetch(u).then(r=>{if(!r.ok)throw new Error(r.status);return r.json()});
const f=(v,d=0)=>v==null||isNaN(v)?'--':(+v).toFixed(d);
const DAY=86400,C={blue:'#2B5F9E',red:'#B3261E',org:'#A84B0C',mut:'#5A564E',grid:'#ECE7DC',axis:'#CFC7B6',grn:'#2F7D4F'};
const ago=t=>{if(!t)return 'never';let s=Date.now()/1e3-t;return s<90?'just now':s<5400?Math.round(s/60)+' min ago':s<172800?Math.round(s/3600)+' h ago':Math.round(s/86400)+' days ago'};
let tab='ov';
$$('nav button').forEach(b=>b.onclick=()=>{$$('nav button,section').forEach(x=>x.classList.remove('on'));b.classList.add('on');$('#'+b.dataset.t).classList.add('on');tab=b.dataset.t;if(tab=='trends'&&!R.loaded)preset(7);if(tab=='se')loadSet();});

/* ---------------- Overview ---------------- */
async function ov(){const s=await j('/api/status');$('#name').textContent=s.device_name;$('#sub').textContent=s.place||'';
 $('#now').textContent=f(s.temp_f)+'°F';$('#cond').textContent=s.condition;$('#pr').textContent=f(s.pressure_inhg,2)+' inHg';$('#tr').textContent=s.trend;
 const rows=[['Firmware',s.firmware],['Uptime',Math.round(s.uptime_s/3600)+' h'],['Address',s.ip+' / '+s.hostname+'.local'],['Wi-Fi',s.wifi],
 ['Weather',s.weather_ok?'<span class=ok>OK</span>, '+ago(s.weather_fetched):'<span class=bad>'+s.weather_error+'</span>'],
 ['Sensor',s.sensor+(s.sensor!='none'?', '+ago(s.last_sensor)+(s.sensor_humidity!=null?', '+s.sensor_humidity+'% RH':''):'')],
 ['Storage',s.storage],['Pain taps logged',s.taps],['Pressure samples',s.pressure_samples],['Backlight',s.backlight+'%'],
 ['Touches (accepted / filtered)',s.touch_ok+' / '+s.touch_filtered],['Free memory',Math.round(s.heap/1024)+' KB + '+Math.round(s.psram/1024)+' KB PSRAM']];
 $('#dev').innerHTML=rows.map(r=>'<tr><td>'+r[0]+'</td><td>'+r[1]+'</td></tr>').join('');}

/* ---------------- Chart engine ----------------
   chart(el,{x0,x1,y0,y1,h,layers,yfmt,tip,group,zoom,xnum})
   layers: {type:'line'|'band'|'dots'|'bars', ...}
   Time axis by default (unix seconds, local time); xnum:true for a numeric x axis. */
const PL=56,PR=14,PT=10,PB=26,groups={};
function niceTicks(a,b,n){const span=b-a||1,raw=span/n,mag=Math.pow(10,Math.floor(Math.log10(raw))),st=[1,2,2.5,5,10].map(m=>m*mag).find(s=>span/s<=n)||raw;
 const out=[];for(let v=Math.ceil(a/st)*st;v<=b+1e-9;v+=st)out.push(+v.toFixed(10));return out}
function timeTicks(x0,x1){const span=x1-x0,H=3600;const steps=[[H,'h'],[3*H,'h'],[6*H,'h'],[12*H,'h'],[DAY,'d'],[2*DAY,'d'],[7*DAY,'w'],[14*DAY,'w'],[0,'m1'],[0,'m3'],[0,'m6'],[0,'y']];
 let pick=steps.find(s=>s[0]&&span/s[0]<=9);if(!pick)pick=span/(30*DAY)<=9?[0,'m1']:span/(91*DAY)<=9?[0,'m3']:span/(182*DAY)<=9?[0,'m6']:[0,'y'];
 const t=[],d=new Date(x0*1e3);
 if(pick[1]=='h'){d.setMinutes(0,0,0);const hs=pick[0]/H;d.setHours(Math.ceil(d.getHours()/hs)*hs);for(;d/1e3<=x1;d.setHours(d.getHours()+hs))t.push(d/1e3)}
 else if(pick[1]=='d'||pick[1]=='w'){d.setHours(0,0,0,0);d.setDate(d.getDate()+1);const ds=pick[0]/DAY;if(pick[1]=='w')while(d.getDay()!=1)d.setDate(d.getDate()+1);for(;d/1e3<=x1;d.setDate(d.getDate()+ds))t.push(d/1e3)}
 else{const ms=pick[1]=='y'?12:+pick[1].slice(1);d.setHours(0,0,0,0);d.setDate(1);d.setMonth(d.getMonth()+1);while(d.getMonth()%ms)d.setMonth(d.getMonth()+1);for(;d/1e3<=x1;d.setMonth(d.getMonth()+ms))t.push(d/1e3)}
 const fmt=v=>{const D=new Date(v*1e3);if(pick[1]=='h')return D.getHours()==0?D.toLocaleDateString(undefined,{weekday:'short',day:'numeric'}):D.toLocaleTimeString(undefined,{hour:'numeric'});
  if(pick[1]=='d'||pick[1]=='w')return D.toLocaleDateString(undefined,{month:'short',day:'numeric'});return D.toLocaleDateString(undefined,{month:'short',year:pick[1]=='m1'&&D.getMonth()?undefined:'numeric'})};
 return t.map(v=>[v,fmt(v)])}
function chart(el,o){const W=o.w||1000,H=o.h||220,X=t=>PL+(t-o.x0)/(o.x1-o.x0)*(W-PL-PR),Y=v=>PT+(1-(v-o.y0)/(o.y1-o.y0))*(H-PT-PB);
 let s='';const yt=o.yticks||niceTicks(o.y0,o.y1,5);
 yt.forEach(v=>{s+=`<line x1="${PL}" x2="${W-PR}" y1="${Y(v)}" y2="${Y(v)}" stroke="${C.grid}"/><text x="${PL-8}" y="${Y(v)+4}" font-size="13" text-anchor="end" fill="${C.mut}">${o.yfmt?o.yfmt(v):v}</text>`});
 const xt=o.xnum?niceTicks(o.x0,o.x1,8).map(v=>[v,o.xfmt?o.xfmt(v):v]):timeTicks(o.x0,o.x1);
 xt.forEach(([v,l])=>{s+=`<line x1="${X(v)}" x2="${X(v)}" y1="${PT}" y2="${H-PB}" stroke="${C.grid}"/><text x="${X(v)}" y="${H-7}" font-size="13" text-anchor="middle" fill="${C.mut}">${l}</text>`});
 if(o.zero!=null)s+=`<line x1="${PL}" x2="${W-PR}" y1="${Y(o.zero)}" y2="${Y(o.zero)}" stroke="${C.axis}" stroke-width="1.5"/>`;
 for(const L of o.layers){
  if(L.type=='band'){let up='',dn='';const seg=()=>{if(up)s+=`<path d="M${up}L${dn.split('L').reverse().join('L')}Z" fill="${L.color}" opacity="${L.opacity||.18}"/>`;up=dn=''};
   L.data.forEach(p=>{if(p[1]==null){seg();return}const x=X(p[0]).toFixed(1);up+=(up?'L':'')+x+' '+Y(p[2]).toFixed(1);dn+=(dn?'L':'')+x+' '+Y(p[1]).toFixed(1)});seg()}
  if(L.type=='line'){let d='',pen=false;L.data.forEach(p=>{if(p[1]==null){pen=false;return}d+=(pen?'L':'M')+X(p[0]).toFixed(1)+' '+Y(p[1]).toFixed(1);pen=true});
   s+=`<path d="${d}" fill="none" stroke="${L.color}" stroke-width="${L.width||3}" stroke-linejoin="round" stroke-linecap="round"${L.dash?` stroke-dasharray="${L.dash}"`:''}/>`}
  if(L.type=='dots')L.data.forEach(p=>{if(p[1]==null)return;const op=p[2]==null?1:p[2],hol=p[3];
   s+=`<circle cx="${X(p[0]).toFixed(1)}" cy="${Y(p[1]).toFixed(1)}" r="${L.r||4.5}" fill="${hol?'#fff':L.color}" stroke="${L.color}" stroke-width="${hol?2:0}" opacity="${op}"/>`});
  if(L.type=='bars'){const bw=Math.max(1.5,(X(o.x0+L.w)-X(o.x0))*.75);L.data.forEach(p=>{if(p[1]==null)return;const c=p[2]||L.color,y=Y(Math.max(p[1],o.y0)),y0=Y(L.base||o.y0);
   s+=`<rect x="${(X(p[0]+(L.center?0:L.w/2))-bw/2).toFixed(1)}" y="${Math.min(y,y0).toFixed(1)}" width="${bw.toFixed(1)}" height="${Math.abs(y0-y).toFixed(1)}" fill="${c}" opacity="${L.opacity||.85}"/>`})}
  if(L.type=='raw')s+=L.svg(X,Y,W)}
 s+=`<line class="xh" x1="0" x2="0" y1="${PT}" y2="${H-PB}" stroke="#1C1B19" stroke-width="1" visibility="hidden"/><rect class="sel" x="0" y="${PT}" width="0" height="${H-PT-PB}" fill="${C.blue}" opacity=".12"/>`;
 el.innerHTML=`<svg viewBox="0 0 ${W} ${H}">${s}<rect class="hit" x="${PL}" y="0" width="${W-PL-PR}" height="${H}" fill="transparent" style="cursor:${o.zoom?'crosshair':'default'}"/></svg><div class="tip"></div>`;
 const svg=el.querySelector('svg'),tip=el.querySelector('.tip'),hit=el.querySelector('.hit'),sel=el.querySelector('.sel');
 const toT=e=>{const b=svg.getBoundingClientRect(),vx=(e.clientX-b.left)/b.width*W;return o.x0+(Math.min(Math.max(vx,PL),W-PR)-PL)/(W-PL-PR)*(o.x1-o.x0)};
 const me={show(t){const xl=svg.querySelector('.xh');xl.setAttribute('x1',X(t));xl.setAttribute('x2',X(t));xl.setAttribute('visibility','visible')},hide(){svg.querySelector('.xh').setAttribute('visibility','hidden');tip.style.display='none'}};
 if(o.group){(groups[o.group]=groups[o.group]||[]).push(me)}
 const peers=()=>o.group?groups[o.group]:[me];let drag=null;
 hit.onpointermove=e=>{const t=toT(e);peers().forEach(p=>p.show(t));const h=o.tip&&o.tip(t);
  if(h){tip.innerHTML=h;tip.style.display='block';const b=el.getBoundingClientRect(),x=e.clientX-b.left;tip.style.left=Math.min(x+14,b.width-tip.offsetWidth-4)+'px';tip.style.top='4px'}else tip.style.display='none';
  if(drag!=null){const a=X(Math.min(drag,t)),z=X(Math.max(drag,t));sel.setAttribute('x',a);sel.setAttribute('width',z-a)}};
 hit.onpointerleave=()=>{peers().forEach(p=>p.hide());if(drag!=null){drag=null;sel.setAttribute('width',0)}};
 if(o.zoom){hit.onpointerdown=e=>{drag=toT(e);hit.setPointerCapture(e.pointerId)};
  hit.onpointerup=e=>{if(drag==null)return;const t=toT(e),a=Math.min(drag,t),b=Math.max(drag,t);drag=null;sel.setAttribute('width',0);if(X(b)-X(a)>12)o.zoom(a,b)};
  hit.ondblclick=()=>{const c=(o.x0+o.x1)/2,s=(o.x1-o.x0);o.zoom(c-s,c+s)}}
 return me}
const nearest=(arr,t,key=0)=>{let best=null,bd=1e18;for(const p of arr){const d=Math.abs(p[key]-t);if(d<bd){bd=d;best=p}}return best};
const dstr=t=>new Date(t*1e3).toLocaleDateString(undefined,{weekday:'short',month:'short',day:'numeric',year:'numeric'});
const tstr=t=>new Date(t*1e3).toLocaleString(undefined,{weekday:'short',month:'short',day:'numeric',hour:'numeric',minute:'2-digit'});

/* ---------------- Trends ---------------- */
const R={from:0,to:0,loaded:false,first:0,daily:[],series:null};
const iso=t=>{const d=new Date(t*1e3);return d.getFullYear()+'-'+String(d.getMonth()+1).padStart(2,'0')+'-'+String(d.getDate()).padStart(2,'0')};
const fromIso=(s,end)=>{const [y,m,d]=s.split('-').map(Number);return new Date(y,m-1,d+(end?1:0))/1e3};
async function preset(d){$$('#presets button').forEach(b=>b.classList.toggle('on',b.dataset.d==String(d)));const now=Date.now()/1e3;
 if(d=='all'){const r=await j('/api/range');R.first=r.first;load(r.first?r.first-3600:now-30*DAY,now);return}
 load(now-d*DAY,now)}
$$('#presets button').forEach(b=>b.onclick=()=>preset(b.dataset.d=='all'?'all':+b.dataset.d));
$('#dgo').onclick=()=>{if($('#dfrom').value&&$('#dto').value){clearPreset();load(fromIso($('#dfrom').value),fromIso($('#dto').value,true))}};
const clearPreset=()=>$$('#presets button').forEach(b=>b.classList.remove('on'));
$('#back').onclick=()=>{const s=R.to-R.from;clearPreset();load(R.from-s,R.from)};
$('#fwd').onclick=()=>{const s=R.to-R.from,now=Date.now()/1e3;clearPreset();load(Math.min(R.to,now-s),Math.min(R.to+s,now))};
$('#bigfall').onchange=()=>{summary();scatter();months()};$('#scLag').onchange=scatter;

async function load(from,to){from=Math.floor(from);to=Math.ceil(Math.min(to,Date.now()/1e3+60));if(to-from<6*3600)from=to-6*3600;
 R.from=from;R.to=to;R.loaded=true;$('#dfrom').value=iso(from);$('#dto').value=iso(to-1);
 const days=(to-from)/DAY;$('#rlabel').textContent=dstr(from)+' – '+dstr(to-1)+'  ('+(days<2?Math.round(days*24)+' hours':Math.round(days)+' days')+')';
 $('#dlRange').href=`/export/daily.csv?from=${from}&to=${to}`;
 $('#trends').classList.add('busy');
 try{
  const q=`?from=${from}&to=${to}`,dq=`?from=${from-DAY}&to=${to}`;
  const [s,d,p,c]=await Promise.all([j('/api/series'+q),j('/api/daily'+dq),j('/api/profile'+q),j('/api/correlation'+q)]);
  R.series=s;R.daily=d.days.map(x=>({...x,t:x.day-d.utc_offset}));R.profile=p;R.corr=c;
  draw()}catch(e){$('#rlabel').textContent='Could not load data ('+e.message+')'}
 $('#trends').classList.remove('busy')}

function rolling(vals,k){return vals.map((_,i)=>{let s=0,n=0;for(let q=i-k;q<=i+k;q++)if(vals[q]!=null){s+=vals[q];n++}return n>=3?s/n:null})}
function draw(){for(const g in groups)delete groups[g];timeCharts();summary();scatter();lagChart();hourChart();months();dailyTable()}

function timeCharts(){const s=R.series,x0=R.from,x1=R.to,span=x1-x0,detail=span<=16*DAY,zoom=(a,b)=>{clearPreset();load(a,b)};
 const days=R.daily.filter(d=>d.t+DAY>x0&&d.t<x1);
 // Pressure
 const pts=s.pressure_inhg.map((v,i)=>[s.start+i*s.step,v]);
 let lo=1e9,hi=-1e9;const upd=v=>{if(v!=null){lo=Math.min(lo,v);hi=Math.max(hi,v)}};
 if(detail)pts.forEach(p=>upd(p[1]));else days.forEach(d=>{upd(d.p_min);upd(d.p_max)});
 if(lo>hi){lo=29.6;hi=30.4}const pad=Math.max(.04,(hi-lo)*.12);
 const playersD=detail?[{type:'line',data:pts,color:C.blue,width:2.5}]:
  [{type:'band',data:days.map(d=>[d.t+DAY/2,d.p_min,d.p_max]),color:C.blue},{type:'line',data:days.map(d=>[d.t+DAY/2,d.p_mean]),color:C.blue,width:2.5}];
 chart($('#cP'),{x0,x1,y0:lo-pad,y1:hi+pad,h:200,layers:playersD,yfmt:v=>v.toFixed(2),group:'t',zoom,
  tip:t=>{if(detail){const p=nearest(pts,t);return p&&p[1]!=null?`${tstr(p[0])}<br><b>${p[1].toFixed(2)} inHg</b>`:''}
   const d=nearest(days,t-DAY/2,'t');return d&&d.p_mean!=null?`${dstr(d.t)}<br>average <b>${d.p_mean.toFixed(2)}</b> inHg<br>range ${d.p_min.toFixed(2)}–${d.p_max.toFixed(2)}`:''}});
 $('#legP').innerHTML=detail?'<span><i style="background:'+C.blue+'"></i>sea-level pressure</span>':'<span><i style="background:'+C.blue+'"></i>daily average</span><span><i style="background:'+C.blue+';opacity:.25;height:10px"></i>daily low–high</span>';
 // Pain
 let painL;
 if(detail){const tp=s.pain.map((v,i)=>[s.start+i*s.step,v]);
  painL=[{type:'line',data:tp,color:C.red,width:3},{type:'dots',data:s.taps.map(t=>[t[0],t[1],.15+.75*t[2]]),color:C.red,r:4}];
  chart($('#cN'),{x0,x1,y0:.7,y1:5.3,h:200,yticks:[1,2,3,4,5],layers:painL,group:'t',zoom,
   tip:t=>{const p=nearest(tp,t);let h=p&&p[1]!=null?`${tstr(p[0])}<br>trend <b>${p[1].toFixed(1)}</b>`:`${tstr(t)}<br>no data`;
    const near=s.taps.filter(q=>Math.abs(q[0]-t)<(x1-x0)/120);if(near.length)h+='<br>taps: '+near.map(q=>q[1]+(q[2]<.5?' (likely mis-tap)':'')).join(', ');return h}});
  $('#legN').innerHTML='<span><i style="background:'+C.red+'"></i>calculated trend</span><span>● tap (faded = judged a likely mis-tap)</span>';
 }else{const dv=days.map(d=>d.pain),roll=rolling(dv,3);
  painL=[{type:'line',data:days.map((d,i)=>[d.t+DAY/2,roll[i]]),color:C.red,width:3},{type:'dots',data:days.map(d=>[d.t+DAY/2,d.pain,1,d.low_conf]),color:C.red,r:span>200*DAY?3:4.5}];
  chart($('#cN'),{x0,x1,y0:.7,y1:5.3,h:200,yticks:[1,2,3,4,5],layers:painL,group:'t',zoom,
   tip:t=>{const i=days.findIndex(d=>t>=d.t&&t<d.t+DAY);if(i<0)return '';const d=days[i];
    return `${dstr(d.t)}<br>`+(d.pain!=null?`day's pain <b>${d.pain.toFixed(1)}</b> (${d.taps} tap${d.taps==1?'':'s'})`:'no taps')+(roll[i]!=null?`<br>7-day average ${roll[i].toFixed(1)}`:'')}});
  $('#legN').innerHTML='<span><i style="background:'+C.red+'"></i>7-day average</span><span>● day\'s pain</span><span>○ day with only 1 tap</span>';}
 // Daily falls
 const big=+$('#bigfall').value||.1;let fmax=.12;days.forEach(d=>{if(d.fall24!=null)fmax=Math.max(fmax,d.fall24)});
 $('#fallWrap').style.display=span>=3*DAY?'':'none';
 if(span>=3*DAY)chart($('#cF'),{x0,x1,y0:0,y1:fmax*1.1,h:130,yfmt:v=>v.toFixed(2),group:'t',zoom,
  layers:[{type:'bars',w:DAY,data:days.map(d=>[d.t,d.fall24,d.fall24>=big?C.org:'#C9B8A4'])},{type:'raw',svg:(X,Y,W)=>`<line x1="${PL}" x2="${W-PR}" y1="${Y(big)}" y2="${Y(big)}" stroke="${C.org}" stroke-dasharray="5 5"/>`}],
  tip:t=>{const d=days.find(d=>t>=d.t&&t<d.t+DAY);return d&&d.fall24!=null?`${dstr(d.t)}<br>largest 24 h fall <b>${d.fall24.toFixed(2)}</b> inHg`+(d.fall24>=big?' (big)':''):''}})}

function stat(arr){const v=arr.filter(x=>x!=null);return v.length?v.reduce((a,b)=>a+b,0)/v.length:null}
function summary(){const days=R.daily.filter(d=>d.t>=R.from-DAY/2&&d.t<R.to),big=+$('#bigfall').value||.1;
 const logged=days.filter(d=>d.taps>0),taps=logged.reduce((a,d)=>a+d.taps,0),total=Math.max(1,Math.round((R.to-R.from)/DAY));
 const bigDays=days.filter(d=>d.fall24!=null&&d.fall24>=big),bigSet=new Set(bigDays.map(d=>d.t));
 const after=logged.filter(d=>bigSet.has(d.t)||bigSet.has(d.t-DAY)),other=logged.filter(d=>!bigSet.has(d.t)&&!bigSet.has(d.t-DAY));
 const pm=days.map(d=>d.p_mean).filter(v=>v!=null),pl=days.map(d=>d.p_min).filter(v=>v!=null),ph=days.map(d=>d.p_max).filter(v=>v!=null);
 const mis=R.series&&R.series.taps.length?R.series.taps.filter(t=>t[2]<.5).length:null;
 const T=[[f(stat(logged.map(d=>d.pain)),1),'average daily pain'],[logged.length+' / '+total,'days with pain logged'],[taps,'taps'+(mis!=null?` (${mis} likely mis-taps)`:'')],
  [pm.length?f(stat(pm),2):'--','average pressure (inHg)'],[pl.length?f(Math.min(...pl),2)+'–'+f(Math.max(...ph),2):'--','pressure range (inHg)'],
  [bigDays.length,'days with a big 24 h fall'],[after.length?f(stat(after.map(d=>d.pain)),1):'--','pain on/after big-fall days'+(after.length?` (${after.length} days)`:'')],
  [other.length?f(stat(other.map(d=>d.pain)),1):'--','pain on other days'+(other.length?` (${other.length} days)`:'')]];
 $('#tiles').innerHTML=T.map(t=>`<div class="tile"><div class="v">${t[0]}</div><div class="k">${t[1]}</div></div>`).join('')}

function scatter(){const lag=+$('#scLag').value,byT=new Map(R.daily.map(d=>[d.t,d]));
 const pts=R.daily.filter(d=>d.t>=R.from-DAY/2&&d.t<R.to&&d.taps>0).map(d=>{const src=lag?byT.get(d.t-DAY):d;return src&&src.fall24!=null?[src.fall24,d.pain,d.low_conf,d.t]:null}).filter(Boolean);
 let xm=.15;pts.forEach(p=>xm=Math.max(xm,p[0]));const n=pts.length;let note;
 let fit=null;if(n>=3){const mx=stat(pts.map(p=>p[0])),my=stat(pts.map(p=>p[1]));let sxy=0,sxx=0,syy=0;pts.forEach(p=>{sxy+=(p[0]-mx)*(p[1]-my);sxx+=(p[0]-mx)**2;syy+=(p[1]-my)**2});
  if(sxx>0&&syy>0){const b=sxy/sxx,r=sxy/Math.sqrt(sxx*syy);fit={a:my-b*mx,b,r}}}
 chart($('#cS'),{w:520,x0:0,x1:xm*1.08,y0:.7,y1:5.3,h:300,xnum:true,xfmt:v=>v.toFixed(2),yticks:[1,2,3,4,5],
  layers:[{type:'dots',data:pts.map(p=>[p[0],p[1],.85,p[2]]),color:C.red,r:5},...(fit?[{type:'line',data:[[0,fit.a],[xm*1.08,fit.a+fit.b*xm*1.08]].map(p=>[p[0],Math.max(.7,Math.min(5.3,p[1]))]),color:C.mut,width:2,dash:'6 5'}]:[])],
  tip:x=>{const p=nearest(pts,x);return p?`${dstr(p[3])}<br>pain ${p[1].toFixed(1)}, fall ${p[0].toFixed(2)} inHg`:''}});
 if(!fit)note=n<3?'Not enough days with both pain taps and pressure data yet.':'No variation to compare yet.';
 else{const s=Math.abs(fit.r)<.2?'little or no':Math.abs(fit.r)<.4?'a weak':Math.abs(fit.r)<.6?'a moderate':'a strong';
  note=`${n} days. r = ${fit.r.toFixed(2)}: ${s} ${fit.r>0?'tendency for more pain with bigger falls':'tendency for less pain with bigger falls'}`+(Math.abs(fit.r)<.2?'.':` (about ${(fit.b*0.1).toFixed(2)} points per 0.10 inHg).`)+(n<20?' With under 20 days this can easily be chance.':'')}
 $('#scNote').textContent='Dashed line: best fit. Hollow dots: days with a single tap. '+note}

function lagChart(){const L=R.corr.lags,cols={3:'#7FA7D6',12:C.blue,24:'#173A66'};
 const layers=[3,12,24].map(w=>({type:'line',data:L.filter(p=>p.window_h==w).map(p=>[p.lag_h,p.r]),color:cols[w],width:3}));
 let best=null;L.forEach(p=>{if(p.r!=null&&(!best||Math.abs(p.r)>Math.abs(best.r)))best=p});
 chart($('#cL'),{w:520,x0:0,x1:48,y0:-1,y1:1,xnum:true,xfmt:v=>v+' h',yticks:[-1,-.5,0,.5,1],zero:0,layers,h:300,
  tip:x=>{const lag=Math.round(x/3)*3;return `pain ${lag} h later<br>`+[3,12,24].map(w=>{const p=L.find(q=>q.window_h==w&&q.lag_h==lag);return `${w} h change: r = ${p&&p.r!=null?p.r.toFixed(2):'--'}`}).join('<br>')}});
 $('#legL').innerHTML=[3,12,24].map(w=>`<span><i style="background:${cols[w]}"></i>pressure change over ${w} h</span>`).join('');
 $('#lagNote').textContent=best?`Strongest: pressure change over ${best.window_h} h, with pain ${best.lag_h} h later (r = ${best.r.toFixed(2)}, ${best.n} hours of data). Below zero means pain tends to rise after pressure falls. Values between -0.2 and 0.2 are weak.`:'Needs at least 2 days of overlapping pain and pressure data.'}

function hourChart(){const P=R.profile,mx=Math.max(1,...P.taps);
 chart($('#cH'),{w:520,x0:0,x1:24,y0:0,y1:5.3,h:280,xnum:true,xfmt:v=>v==0||v==24?'12a':v==12?'12p':v<12?v+'a':(v-12)+'p',yticks:[1,2,3,4,5],
  layers:[{type:'bars',w:1,data:P.taps.map((n,h)=>[h,n/mx*5]),color:'#E3DCCD',opacity:1},{type:'bars',w:1,data:P.pain.map((v,h)=>[h,v]),color:C.red,opacity:.8}],
  tip:x=>{const h=Math.min(23,Math.floor(x));return `${h%12||12}${h<12?' AM':' PM'} hour<br>`+(P.pain[h]!=null?`average ${P.pain[h].toFixed(1)}, `:'')+`${P.taps[h]} taps`}})}

function months(){const big=+$('#bigfall').value||.1,M=new Map();
 R.daily.filter(d=>d.t>=R.from-DAY/2&&d.t<R.to).forEach(d=>{const D=new Date((d.t+DAY/2)*1e3),k=D.getFullYear()*100+D.getMonth();
  if(!M.has(k))M.set(k,{label:D.toLocaleDateString(undefined,{month:'short',year:'numeric'}),days:[]});M.get(k).days.push(d)});
 const rows=[...M.entries()].sort((a,b)=>b[0]-a[0]).map(([k,m])=>{const lg=m.days.filter(d=>d.taps>0),bd=m.days.filter(d=>d.fall24!=null&&d.fall24>=big);
  return `<tr><td>${m.label}</td><td>${f(stat(lg.map(d=>d.pain)),1)}</td><td>${lg.length}</td><td>${lg.reduce((a,d)=>a+d.taps,0)}</td><td>${f(stat(m.days.map(d=>d.p_mean)),2)}</td><td>${bd.length}</td></tr>`});
 $('#months').innerHTML='<tr><th>Month</th><th>Avg pain</th><th>Days logged</th><th>Taps</th><th>Avg pressure</th><th>Big-fall days</th></tr>'+rows.join('')}

function dailyTable(){const big=+$('#bigfall').value||.1;
 $('#daily').innerHTML='<tr><th>Date</th><th>Pain</th><th>Taps</th><th>Pressure (avg, low–high)</th><th>Largest 24 h fall</th></tr>'+R.daily.filter(d=>d.t>=R.from-DAY/2&&d.t<R.to).slice().reverse().map(d=>
  `<tr><td>${dstr(d.t)}</td><td>${d.pain!=null?f(d.pain,1):'--'}${d.taps&&d.low_conf?' <span class=pill>1 tap</span>':''}</td><td>${d.taps}</td><td>${d.p_mean!=null?f(d.p_mean,2)+' ('+f(d.p_min,2)+'–'+f(d.p_max,2)+')':'--'}</td><td>${d.fall24==null?'--':(d.fall24>=big?'<b class=bad>':'')+f(d.fall24,2)+' inHg'+(d.fall24>=big?'</b>':'')}</td></tr>`).join('')}

/* ---------------- Settings / Data ---------------- */
async function loadSet(){const s=await j('/api/settings');for(const k in s){$$('[name='+k+']').forEach(e=>{if(e.type!='password')e.value=s[k]})}$('#loc').textContent=s.place?'Current: '+s.place:'No location set';}
let places=[];$('#qs').onclick=async()=>{const r=await j('/api/geocode?q='+encodeURIComponent($('#q').value));places=r;const sel=$('#qr');
 sel.innerHTML=r.map((p,i)=>`<option value="${i}">${p.name}, ${p.region}</option>`).join('');sel.style.display=r.length?'block':'none';if(r.length)pick(0);else $('#loc').textContent='Nothing found'};
$('#qr').onchange=e=>pick(+e.target.value);
function pick(i){const p=places[i],F=$('#f1');F.place.value=p.name+', '+p.region;F.lat.value=p.lat;F.lon.value=p.lon;F.elevation_m.value=p.elevation;$('#loc').textContent='Will use: '+F.place.value}
['#f1','#f2','#f3','#f4','#f5'].forEach(id=>$(id).onsubmit=async e=>{e.preventDefault();const fd=new URLSearchParams(new FormData(e.target));
 for(const [k,v] of [...fd])if(v==='')fd.delete(k);const r=await fetch('/api/settings',{method:'POST',body:fd});alert(r.ok?'Saved':'Error: '+await r.text());loadSet();ov()});
$('#rb').onclick=()=>{if(confirm('Restart the station?'))fetch('/api/restart',{method:'POST'})};
$('#up').onsubmit=e=>{e.preventDefault();const fd=new FormData(e.target);$('#upm').textContent='Uploading...';fetch('/update',{method:'POST',body:fd}).then(r=>r.text()).then(t=>$('#upm').textContent=t).catch(()=>$('#upm').textContent='Upload failed')};
ov();setInterval(()=>{if(tab=='ov')ov()},30000);
</script></body></html>)HTML";
