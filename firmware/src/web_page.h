// The single-page web management interface. Served from flash so OTA updates it too.
#pragma once

static const char WEB_PAGE[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Weather &amp; Pain Station</title>
<style>
:root{--bg:#F6F3EC;--card:#fff;--ink:#1C1B19;--mut:#5A564E;--rule:#DDD6C8;--blue:#2B5F9E;--red:#B3261E;--org:#A84B0C}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.45 system-ui,-apple-system,Segoe UI,sans-serif}
header{display:flex;flex-wrap:wrap;gap:8px 24px;align-items:baseline;padding:16px 24px;border-bottom:2px solid var(--rule)}
h1{font-size:22px;margin:0}#sub{color:var(--mut)}
nav{display:flex;gap:4px;padding:0 24px;border-bottom:2px solid var(--rule);flex-wrap:wrap}
nav button{background:none;border:0;padding:12px 14px;font:inherit;color:var(--mut);cursor:pointer;border-bottom:3px solid transparent;margin-bottom:-2px}
nav button.on{color:var(--ink);border-color:var(--blue);font-weight:600}
main{padding:20px 24px;max-width:1100px}section{display:none}section.on{display:block}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:16px}
.card{background:var(--card);border:1px solid var(--rule);border-radius:10px;padding:16px 18px;margin-bottom:16px}
.card h2{font-size:15px;text-transform:uppercase;letter-spacing:.05em;color:var(--mut);margin:0 0 10px}
.big{font-size:40px;font-weight:700;line-height:1.1}
table{border-collapse:collapse;width:100%}td,th{text-align:left;padding:6px 8px;border-bottom:1px solid var(--rule);font-variant-numeric:tabular-nums}
th{color:var(--mut);font-weight:600;font-size:14px}.kv td:first-child{color:var(--mut);width:42%}
label{display:block;margin:10px 0 4px;color:var(--mut);font-size:14px}
input,select{font:inherit;padding:8px 10px;border:1px solid #bdb5a5;border-radius:8px;width:100%;background:#fff}
.row{display:flex;gap:10px;align-items:end}.row>*{flex:1}
button.b{font:inherit;padding:9px 16px;border-radius:8px;border:0;background:var(--blue);color:#fff;cursor:pointer;margin-top:12px}
button.alt{background:#E6E0D3;color:var(--ink)}a{color:var(--blue)}
svg{width:100%;height:auto;display:block}.note{color:var(--mut);font-size:14px}.ok{color:#2F7D4F}.bad{color:var(--org)}
.pill{display:inline-block;padding:2px 8px;border-radius:99px;background:#E6E0D3;font-size:13px}
</style></head><body>
<header><h1 id="name">Weather &amp; Pain Station</h1><span id="sub"></span></header>
<nav><button data-t="ov" class="on">Overview</button><button data-t="hi">History &amp; analysis</button><button data-t="se">Settings</button><button data-t="da">Data &amp; updates</button></nav>
<main>
<section id="ov" class="on"><div class="grid">
 <div class="card"><h2>Now</h2><div class="big" id="now">--</div><div id="cond" class="note"></div>
  <table class="kv" style="margin-top:10px"><tr><td>Pressure</td><td id="pr">--</td></tr><tr><td>Trend (3 h)</td><td id="tr">--</td></tr></table></div>
 <div class="card"><h2>Device</h2><table class="kv" id="dev"></table></div>
</div></section>

<section id="hi">
 <div class="card"><div class="row" style="max-width:420px"><div><label>Range</label><select id="rng"><option value="48">2 days</option><option value="168">7 days</option><option value="720">30 days</option><option value="2160">90 days</option></select></div></div>
  <h2 style="margin-top:16px">Pressure (inHg)</h2><div id="cp"></div>
  <h2 style="margin-top:16px">Pain trend (1&ndash;5) with taps</h2><div id="cn"></div>
  <p class="note">Line = calculated trend. Dots = raw taps; faded dots were judged likely mis-taps and count for less.</p></div>
 <div class="card"><h2>Pressure change vs. pain</h2><div id="corr" class="note">Loading&hellip;</div></div>
 <div class="card"><h2>Daily values</h2><div style="max-height:420px;overflow:auto"><table id="daily"></table></div></div>
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
  <p><a href="/export/pressure.csv">pressure.csv</a> &ndash; logged pressure</p><p><a href="/export/daily.csv">daily.csv</a> &ndash; calculated daily values</p>
  <p class="note">Times are Unix seconds (UTC). tools/analyze.py reproduces the analysis on a computer.</p></div>
 <form class="card" id="up" method="POST" action="/update" enctype="multipart/form-data"><h2>Firmware update</h2>
  <input type="file" name="fw" accept=".bin"><button class="b">Upload &amp; install</button>
  <p class="note">Use firmware.bin from <code>.pio/build/station/</code>. The station restarts and rolls back automatically if the new version fails to start.</p><div id="upm"></div></form>
 <div class="card"><h2>Restart</h2><button class="b alt" id="rb">Restart station</button></div>
</div></section>
</main>
<script>
const $=s=>document.querySelector(s),$$=s=>document.querySelectorAll(s);
$$('nav button').forEach(b=>b.onclick=()=>{$$('nav button,section').forEach(x=>x.classList.remove('on'));b.classList.add('on');$('#'+b.dataset.t).classList.add('on');if(b.dataset.t=='hi')hist();if(b.dataset.t=='se')loadSet();});
const j=u=>fetch(u).then(r=>r.json());
const f=(v,d=0)=>v==null||isNaN(v)?'--':(+v).toFixed(d);
const ago=t=>{if(!t)return 'never';let s=Date.now()/1e3-t;return s<90?'just now':s<5400?Math.round(s/60)+' min ago':s<172800?Math.round(s/3600)+' h ago':Math.round(s/86400)+' days ago'};
async function ov(){const s=await j('/api/status');$('#name').textContent=s.device_name;$('#sub').textContent=s.place||'';
 $('#now').textContent=f(s.temp_f)+'°F';$('#cond').textContent=s.condition;$('#pr').textContent=f(s.pressure_inhg,2)+' inHg';$('#tr').textContent=s.trend;
 const rows=[['Firmware',s.firmware],['Uptime',Math.round(s.uptime_s/3600)+' h'],['Address',s.ip+' / '+s.hostname+'.local'],['Wi-Fi',s.wifi],
 ['Weather',s.weather_ok?'<span class=ok>OK</span>, '+ago(s.weather_fetched):'<span class=bad>'+s.weather_error+'</span>'],
 ['Sensor',s.sensor+(s.sensor!='none'?', '+ago(s.last_sensor)+(s.sensor_humidity!=null?', '+s.sensor_humidity+'% RH':''):'')],
 ['Storage',s.storage],['Pain taps logged',s.taps],['Pressure samples',s.pressure_samples],['Backlight',s.backlight+'%'],
 ['Touches (accepted / filtered)',s.touch_ok+' / '+s.touch_filtered],['Free memory',Math.round(s.heap/1024)+' KB + '+Math.round(s.psram/1024)+' KB PSRAM']];
 $('#dev').innerHTML=rows.map(r=>'<tr><td>'+r[0]+'</td><td>'+r[1]+'</td></tr>').join('');}
function chart(el,start,step,vals,lo,hi,color,taps){const W=1000,H=220,P=34,n=vals.length;const X=i=>P+i*(W-2*P)/(n-1),Y=v=>H-24-(v-lo)/(hi-lo)*(H-40);
 let d='',pen=false;vals.forEach((v,i)=>{if(v==null){pen=false;return}d+=(pen?'L':'M')+X(i).toFixed(1)+' '+Y(v).toFixed(1);pen=true});
 let g='';for(let k=0;k<=4;k++){const v=lo+(hi-lo)*k/4;g+=`<line x1="${P}" x2="${W-P}" y1="${Y(v)}" y2="${Y(v)}" stroke="#eee"/><text x="2" y="${Y(v)+4}" font-size="12" fill="#5A564E">${v.toFixed(hi-lo<2?2:0)}</text>`}
 const days=Math.round(n*step/86400);for(let i=0;i<n;i+=Math.max(1,Math.round(86400/step))){const t=new Date((start+i*step)*1e3);g+=`<text x="${X(i)}" y="${H-4}" font-size="12" fill="#5A564E" text-anchor="middle">${t.toLocaleDateString(undefined,{month:'short',day:'numeric'})}</text>`}
 let dots='';(taps||[]).forEach(t=>{const i=(t[0]-start)/step;dots+=`<circle cx="${X(i)}" cy="${Y(t[1])}" r="4" fill="${color}" opacity="${0.15+0.7*t[2]}"/>`});
 el.innerHTML=`<svg viewBox="0 0 ${W} ${H}">${g}${dots}<path d="${d}" fill="none" stroke="${color}" stroke-width="3" stroke-linejoin="round"/></svg>`}
async function hist(){const h=+$('#rng').value;const s=await j('/api/series?hours='+h);const p=s.pressure_inhg.filter(v=>v!=null);
 const lo=p.length?Math.min(...p)-.03:29.6,hi=p.length?Math.max(...p)+.03:30.4;chart($('#cp'),s.start,s.step,s.pressure_inhg,lo,hi,'#2B5F9E');chart($('#cn'),s.start,s.step,s.pain,1,5,'#B3261E',s.taps);
 const a=await j('/api/analysis?days='+Math.max(14,Math.min(365,Math.round(h/24))));
 $('#daily').innerHTML='<tr><th>Date</th><th>Pain</th><th>Taps</th><th>Pressure</th><th>Largest 24 h fall</th></tr>'+a.daily.slice().reverse().map(d=>`<tr><td>${new Date(d.day*1e3).toISOString().slice(0,10)}</td><td>${f(d.pain,1)}${d.low_conf?' <span class=pill>few taps</span>':''}</td><td>${d.taps}</td><td>${f(d.pressure_inhg,2)}</td><td>${d.max_24h_drop_inhg==null?'--':f(d.max_24h_drop_inhg,2)+' inHg'}</td></tr>`).join('');
 const by={};a.correlation.forEach(c=>{(by[c.window_h]=by[c.window_h]||[]).push(c)});let t='<table><tr><th>Pressure change over</th><th>Strongest link</th><th>Pain follows by</th><th>Hours of data</th></tr>';
 for(const w in by){const best=by[w].filter(c=>c.r!=null).sort((x,y)=>Math.abs(y.r)-Math.abs(x.r))[0];t+=`<tr><td>${w} h</td><td>${best?'r = '+best.r.toFixed(2):'not enough data'}</td><td>${best?best.lag_h+' h':'--'}</td><td>${best?best.n:0}</td></tr>`}
 $('#corr').innerHTML=t+'</table><p>r runs from -1 to 1. A negative r means pain tends to be higher after pressure <b>falls</b>. |r| under about 0.2 is weak; it needs a few weeks of regular taps to mean much.</p>'}
$('#rng').onchange=hist;
async function loadSet(){const s=await j('/api/settings');for(const k in s){$$('[name='+k+']').forEach(e=>{if(e.type!='password')e.value=s[k]})}$('#loc').textContent=s.place?'Current: '+s.place:'No location set';}
let places=[];$('#qs').onclick=async()=>{const r=await j('/api/geocode?q='+encodeURIComponent($('#q').value));places=r;const sel=$('#qr');
 sel.innerHTML=r.map((p,i)=>`<option value="${i}">${p.name}, ${p.region}</option>`).join('');sel.style.display=r.length?'block':'none';if(r.length)pick(0);else $('#loc').textContent='Nothing found'};
$('#qr').onchange=e=>pick(+e.target.value);
function pick(i){const p=places[i],F=$('#f1');F.place.value=p.name+', '+p.region;F.lat.value=p.lat;F.lon.value=p.lon;F.elevation_m.value=p.elevation;$('#loc').textContent='Will use: '+F.place.value}
['#f1','#f2','#f3','#f4','#f5'].forEach(id=>$(id).onsubmit=async e=>{e.preventDefault();const fd=new URLSearchParams(new FormData(e.target));
 for(const [k,v] of [...fd])if(v==='')fd.delete(k);const r=await fetch('/api/settings',{method:'POST',body:fd});alert(r.ok?'Saved':'Error: '+await r.text());loadSet();ov()});
$('#rb').onclick=()=>{if(confirm('Restart the station?'))fetch('/api/restart',{method:'POST'})};
$('#up').onsubmit=e=>{e.preventDefault();const fd=new FormData(e.target);$('#upm').textContent='Uploading...';fetch('/update',{method:'POST',body:fd}).then(r=>r.text()).then(t=>$('#upm').textContent=t).catch(()=>$('#upm').textContent='Upload failed')};
ov();setInterval(ov,30000);
</script></body></html>)HTML";
