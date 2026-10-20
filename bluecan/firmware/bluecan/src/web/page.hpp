// MIT License
//
// Copyright (c) 2026 Dezider Mesko <dezo.mesko@pm.me>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
#pragma once

/// The Wi-Fi mode's one page. Everything on it comes from /stats.json, once a second,
/// and /files.json; it needs nothing from the internet, which the phone does not have
/// on this network.
constexpr char page_html[] = R"html(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Bluecan</title>
<style>
:root{--bg:#fff;--fg:#1b1b1b;--dim:#6b6b6b;--line:#e2e2e2;--accent:#7b3fb3;--bad:#b3261e;--ok:#1e7b34}
@media (prefers-color-scheme:dark){:root{--bg:#141414;--fg:#ececec;--dim:#9a9a9a;--line:#2c2c2c;--accent:#b38be0;--bad:#f2867d;--ok:#7fd18f}}
*{box-sizing:border-box}
body{margin:0;padding:16px;background:var(--bg);color:var(--fg);font:15px/1.4 system-ui,sans-serif;max-width:760px}
h1{font-size:20px;margin:0 0 4px}
h2{font-size:15px;margin:22px 0 6px;color:var(--accent);text-transform:uppercase;letter-spacing:.04em}
.sub{color:var(--dim);font-size:13px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:6px 14px}
.k{color:var(--dim);font-size:12px}
.v{font-size:18px;font-variant-numeric:tabular-nums}
table{border-collapse:collapse;width:100%;font-variant-numeric:tabular-nums}
td,th{padding:4px 6px;border-bottom:1px solid var(--line);text-align:right;white-space:nowrap}
th{color:var(--dim);font-weight:normal;font-size:12px}
td:first-child,th:first-child{text-align:left}
td.data{font-family:ui-monospace,monospace;text-align:left;font-size:13px;white-space:normal;min-width:9em}
.wrap{overflow-x:auto}
button{font:inherit;padding:8px 14px;border-radius:8px;border:1px solid var(--accent);background:none;color:var(--accent)}
button.main{background:var(--accent);color:var(--bg)}
a{color:var(--accent)}
.bad{color:var(--bad)}.ok{color:var(--ok)}
#gone{display:none;padding:12px;border:1px solid var(--line);border-radius:8px;margin-top:12px}
#dead{display:none;position:sticky;top:0;padding:12px;margin:0 0 12px;border-radius:8px;background:var(--bad);color:var(--bg)}
</style>
</head>
<body>
<div id="dead"><b>Bluecan is not answering.</b> It has most likely gone back to the normal mode &ndash; by its
button, after a while without this page, or because the bike moved &ndash; or this phone has left its network.
This page no longer updates. To come back, hold the board's button for 5&nbsp;s and join its network again.</div>
<h1>Bluecan, Wi-Fi mode</h1>
<div class="sub"><span id="ssid">-</span> &middot; up <span id="up">-</span> &middot; <span id="link">connecting</span></div>
<p class="sub">Bluetooth is off in this mode, and nothing is logged. The board goes back to the normal mode
when you press the button below, hold its button for 5 s, leave this page closed for <span id="idle">5 minutes</span>,
or ride off.</p>
<button class="main" id="leave">Back to the normal mode</button>
<div id="gone">Restarting in the normal mode. RaceChrono can connect again in a few seconds; this page can be closed.</div>

<h2>Bus</h2>
<div class="grid">
<div><div class="k">State</div><div class="v" id="state">-</div></div>
<div><div class="k">Frames a second</div><div class="v" id="rate">-</div></div>
<div><div class="k">Frames</div><div class="v" id="frames">-</div></div>
<div><div class="k">Error interrupts</div><div class="v" id="errors">-</div></div>
<div><div class="k">TEC / REC</div><div class="v" id="terec">-</div></div>
<div><div class="k">Trips</div><div class="v" id="trips">-</div></div>
<div><div class="k">29-bit / remote</div><div class="v" id="extrem">-</div></div>
<div><div class="k">Dropped</div><div class="v" id="dropped">-</div></div>
</div>

<h2>Bike</h2>
<div class="grid">
<div><div class="k">Engine, rpm</div><div class="v" id="rpm">-</div></div>
<div><div class="k">Gear</div><div class="v" id="gear">-</div></div>
<div><div class="k">Rear wheel, km/h</div><div class="v" id="rear">-</div></div>
<div><div class="k">Front wheel, km/h</div><div class="v" id="front">-</div></div>
<div><div class="k">Water, &deg;C</div><div class="v" id="water">-</div></div>
<div><div class="k">Air, &deg;C</div><div class="v" id="air">-</div></div>
</div>

<h2>Ids</h2>
<div class="wrap"><table>
<thead><tr><th>Id</th><th>Hz</th><th>Frames</th><th>Last</th><th style="text-align:left">Payload</th></tr></thead>
<tbody id="ids"></tbody>
</table></div>
<div class="sub" id="idsnote"></div>

<div id="cardbox" hidden>
<h2>Card</h2>
<div id="card" class="sub">-</div>
<div class="wrap"><table>
<thead><tr><th>File</th><th>Size</th><th></th></tr></thead>
<tbody id="files"></tbody>
</table></div>
<p><button id="refresh">Refresh the list</button></p>
</div>

<h2>Board</h2>
<div class="grid">
<div><div class="k">Free heap, KB</div><div class="v" id="heap">-</div></div>
<div><div class="k">Least free, KB</div><div class="v" id="heapmin">-</div></div>
<div><div class="k">Devices on the network</div><div class="v" id="stations">-</div></div>
</div>

<script>
const $=id=>document.getElementById(id);
const n=v=>v==null?'-':v.toLocaleString('en-GB');
const x100=v=>v==null?'-':(v/100).toFixed(1);
const x10=v=>v==null?'-':(v/10).toFixed(1);
function dur(s){const h=Math.floor(s/3600),m=Math.floor(s%3600/60),r=s%60;return (h?h+' h ':'')+(h||m?m+' min ':'')+r+' s'}
function size(b){return b<1e6?(b/1e3).toFixed(1)+' kB':(b/1e6).toFixed(1)+' MB'}
let stopped=false,failed=0,timer=null,busy=false;
async function stats(){
  timer=null;
  // Not while the page is out of sight: a background tab that went on asking, even once
  // a minute, would keep the board in this mode for good. It asks again on coming back.
  if(stopped||busy||document.hidden)return;
  busy=true;
  try{
    const ctl=new AbortController();const t=setTimeout(()=>ctl.abort(),3000);
    const r=await fetch('/stats.json',{cache:'no-store',signal:ctl.signal});clearTimeout(t);const s=await r.json();
    failed=0;$('dead').style.display='none';
    $('link').textContent='live';$('link').className='ok';
    $('ssid').textContent=s.ssid;$('up').textContent=dur(s.up_s);$('idle').textContent=s.idle_s<120?s.idle_s+' s':Math.round(s.idle_s/60)+' minutes';
    const b=s.bus;
    $('state').textContent=b.state;$('state').className='v '+(b.state=='acknowledging'?'ok':b.state.startsWith('tripped')?'bad':'');
    $('rate').textContent=n(b.rate);$('frames').textContent=n(b.frames);$('errors').textContent=n(b.errors);
    $('terec').textContent=b.tec+' / '+b.rec;$('trips').textContent=n(b.trips);
    $('extrem').textContent=n(b.extended)+' / '+n(b.remote);$('dropped').textContent=n(b.dropped);
    const k=s.bike;
    $('rpm').textContent=n(k.rpm);
    $('gear').textContent=k.gear==null?'-':k.gear==0?'N':k.gear==7?'between':k.gear;
    $('rear').textContent=x100(k.rear_x100);$('front').textContent=x100(k.front_x100);
    $('water').textContent=x10(k.water_x10);$('air').textContent=x10(k.air_x10);
    const ids=s.ids.slice().sort((a,c)=>a.id-c.id);
    $('ids').innerHTML=ids.map(i=>'<tr><td>0x'+i.id.toString(16).toUpperCase().padStart(3,'0')+'</td><td>'+i.hz+
      '</td><td>'+n(i.n)+'</td><td>'+(i.age_ms<1000?i.age_ms+' ms':dur(Math.round(i.age_ms/1000)))+
      '</td><td class="data">'+i.data.replace(/(..)/g,'$1 ').trim()+'</td></tr>').join('');
    $('idsnote').textContent=ids.length+' ids'+(b.overflow?', and '+n(b.overflow)+' frames of ids beyond the table\'s 32':'');
    $('heap').textContent=Math.round(s.heap/1024);$('heapmin').textContent=Math.round(s.heap_min/1024);
    $('stations').textContent=s.stations;
  }catch(e){
    $('link').textContent='no answer';$('link').className='bad';
    // Three seconds of nothing: the board has gone, not a request lost.
    if(++failed>=3)$('dead').style.display='block';
  }
  busy=false;
  timer=setTimeout(stats,1000);
}
async function files(){
  $('card').textContent='reading the card...';
  try{
    const r=await fetch('/files.json',{cache:'no-store'});const f=await r.json();
    $('cardbox').hidden=!f.card;if(!f.card)return;
    const l=f.files.sort((a,c)=>c.name.localeCompare(a.name));
    $('card').textContent=l.length+' log files, '+size(l.reduce((t,a)=>t+a.size,0))+'. Newest first.';
    $('files').innerHTML=l.map(a=>'<tr><td>'+a.name+'</td><td>'+size(a.size)+
      '</td><td><a href="/file?name='+a.name+'" download="'+a.name+'">download</a></td></tr>').join('');
  }catch(e){$('card').textContent='The card did not answer.'}
}
$('refresh').onclick=files;
document.addEventListener('visibilitychange',()=>{if(!document.hidden&&!timer&&!busy)stats()});
$('leave').onclick=async()=>{
  if(!confirm('Go back to the normal mode?'))return;
  try{await fetch('/leave',{method:'POST'})}catch(e){}
  stopped=true;$('gone').style.display='block';$('leave').disabled=true;$('link').textContent='left';
};
stats();files();
</script>
</body>
</html>
)html";
