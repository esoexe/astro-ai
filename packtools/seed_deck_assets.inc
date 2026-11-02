// Generated from seed-skill/seed-deck/deck.py (MIT, Jake Schincariol): the deck's CSS (a Python %-format) and JS, verbatim.
inline const char* const CSS = R"deck(
:root{--brand:%(brand)s;--ink:%(ink)s;--accent:%(accent)s;--soft:%(soft)s;--soft2:%(soft2)s;--line:%(line)s;
--muted:%(muted)s;--paper:#FFFFFF;--on-brand:%(on_brand)s;--fit:1}
*{box-sizing:border-box}
html,body{margin:0;padding:0}
body{background:%(stage)s;color:var(--ink);font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,
"Helvetica Neue",Arial,sans-serif;-webkit-font-smoothing:antialiased}
.deck{display:flex;flex-direction:column;align-items:center;gap:32px;padding:32px 0}
.slide{width:1280px;height:720px;flex:none;position:relative;display:flex;flex-direction:column;background:var(--paper);
padding:64px 88px 72px;overflow:hidden;box-shadow:0 18px 50px rgba(0,0,0,.28)}
.slide h2{margin:0 0 28px;font-size:46px;line-height:1.08;letter-spacing:-.02em;font-weight:750}
.slide h2::after{content:"";display:block;width:56px;height:6px;border-radius:3px;background:var(--accent);margin-top:18px}
.brandbar{position:absolute;top:30px;right:88px;display:flex;align-items:center;gap:10px;font-size:17px;font-weight:650;
color:var(--muted)}
.mark{width:30px;height:30px;border-radius:9px;background:var(--brand);color:#fff;display:grid;place-items:center;
font-weight:800;font-size:17px;line-height:1}
.num{position:absolute;right:88px;bottom:30px;font-size:16px;color:var(--muted);font-variant-numeric:tabular-nums}
.foot{position:absolute;left:88px;right:180px;bottom:28px;font-size:14px;line-height:1.35;color:var(--muted)}
.body{flex:1;display:flex;flex-direction:column;gap:30px;min-height:0;padding-bottom:64px}
.body>*:last-child:not(:first-child),.s-team .body>*{margin-top:auto;margin-bottom:auto}
.lead{font-size:38px;line-height:1.2;font-weight:600;letter-spacing:-.01em;margin:0;max-width:1040px}
.meta{font-size:22px;color:var(--muted);margin:0;font-weight:600}
.points{list-style:none;margin:0;padding:0;display:grid;grid-template-columns:repeat(3,1fr);gap:22px}
.points li{background:var(--soft);border-radius:18px;padding:28px 26px;font-size:26px;line-height:1.25;font-weight:600;
display:flex;gap:14px;align-items:flex-start}
.points svg{flex:none;margin-top:3px}
.stats{display:grid;grid-template-columns:repeat(2,1fr);gap:28px}
.stat{border:2px solid var(--line);border-radius:22px;padding:30px 32px}
.stat b{display:block;font-size:72px;line-height:1;letter-spacing:-.03em;color:var(--brand);font-variant-numeric:tabular-nums}
.stat span{display:block;margin-top:14px;font-size:24px;line-height:1.3;color:var(--ink)}
.market{display:flex;align-items:center;gap:64px}
.market svg{flex:none}
.legend{list-style:none;margin:0;padding:0;display:flex;flex-direction:column;gap:22px}
.legend li{display:flex;align-items:baseline;gap:18px;font-size:24px}
.legend .tag{width:74px;font-weight:800;color:var(--muted);font-size:20px;letter-spacing:.06em}
.legend b{font-size:46px;letter-spacing:-.02em;min-width:190px;font-variant-numeric:tabular-nums}
.cards{display:grid;gap:22px}
.card{border:2px solid var(--line);border-radius:20px;padding:26px 26px 28px;display:flex;flex-direction:column;gap:10px}
.card h3{margin:0;font-size:30px;letter-spacing:-.01em}
.card .sub{font-size:20px;color:var(--muted)}
.card .lab{margin-top:18px;font-size:15px;text-transform:uppercase;letter-spacing:.08em;color:var(--muted);font-weight:700}
.card .val{font-size:26px;font-weight:650;line-height:1.25}
.card.us{background:var(--brand);border-color:var(--brand);color:var(--on-brand)}
.card.us .val{font-size:24px;margin-top:auto}
.tiles{display:grid;gap:18px}
.tile{background:var(--soft);border-radius:20px;padding:28px 22px}
.tile b{display:block;font-size:50px;letter-spacing:-.03em;line-height:1;font-variant-numeric:tabular-nums;white-space:nowrap}
.tile b small{font-size:26px;letter-spacing:0;margin-left:4px;font-weight:700}
.tiles.five .tile b{font-size:44px}
.tile span{display:block;margin-top:12px;font-size:21px;color:var(--muted)}
.steps{list-style:none;margin:0;padding:0;display:grid;grid-template-columns:repeat(3,1fr);gap:22px;counter-reset:s}
.steps li{position:relative;padding:74px 26px 28px;border:2px solid var(--line);border-radius:20px;font-size:26px;
line-height:1.25;font-weight:600}
.steps li::before{counter-increment:s;content:counter(s);position:absolute;top:24px;left:26px;width:38px;height:38px;
border-radius:50%%;background:var(--accent);color:var(--ink);display:grid;place-items:center;font-size:20px;font-weight:800}
.team{display:grid;gap:26px}
.person{display:flex;gap:22px;align-items:flex-start;background:var(--soft);border-radius:22px;padding:30px}
.face{flex:none;width:84px;height:84px;border-radius:50%%;background:var(--brand);color:#fff;display:grid;place-items:center;
font-size:30px;font-weight:800}
.person h3{margin:4px 0 4px;font-size:30px}
.person .role{font-size:20px;color:var(--muted);font-weight:650}
.person p{margin:12px 0 0;font-size:23px;line-height:1.3}
.fin{display:flex;gap:56px;align-items:flex-end;flex:1;min-height:0}
.side{display:flex;flex-direction:column;gap:20px;flex:1}
.side div{border-left:6px solid var(--accent);padding:6px 0 6px 22px}
.side b{display:block;font-size:46px;letter-spacing:-.02em;line-height:1.05}
.side span{display:block;font-size:21px;color:var(--muted);margin-top:6px}
.funds{display:flex;flex-direction:column;gap:18px;max-width:1100px}
.fund{display:grid;grid-template-columns:360px 1fr 200px;align-items:center;gap:22px;font-size:24px}
.fund .track{height:26px;border-radius:13px;background:var(--soft2);overflow:hidden}
.fund .fill{height:100%%;border-radius:13px;background:var(--brand)}
.fund .amt{font-weight:700;text-align:right;font-variant-numeric:tabular-nums}
.s-title{background:var(--brand);color:var(--on-brand);justify-content:center}
.s-title .body{justify-content:center;gap:26px}
.s-title h1{margin:0;font-size:120px;line-height:1;letter-spacing:-.04em;font-weight:800;display:flex;align-items:center;gap:30px}
.s-title .bigmark{width:112px;height:112px;border-radius:30px;background:var(--on-brand);color:var(--brand);display:grid;
place-items:center;font-size:66px;font-weight:800}
.s-title .tagline{font-size:46px;line-height:1.15;font-weight:600;margin:0;max-width:1000px}
.s-title .meta{color:var(--on-brand);opacity:.85;font-size:26px;display:inline-flex;align-items:center;gap:14px}
.s-title .meta::before{content:"";width:40px;height:6px;border-radius:3px;background:var(--accent)}
.s-title .foot,.s-title .num{color:var(--on-brand);opacity:.8}
.hud{display:none}
html.js body{overflow:hidden;height:100vh}
html.js .deck{position:fixed;inset:0;display:block;padding:0}
html.js .slide{position:absolute;left:50%%;top:50%%;transform:translate(-50%%,-50%%) scale(var(--fit));display:none}
html.js .slide.on{display:flex}
html.js .hud{display:block;position:fixed;left:16px;bottom:12px;font-size:13px;color:rgba(255,255,255,.7);
background:rgba(0,0,0,.45);padding:4px 10px;border-radius:6px;opacity:0;transition:opacity .4s;pointer-events:none}
html.js .hud.show{opacity:1}
@page{size:13.333in 7.5in;margin:0}
@media print{
html,body,html.js body{background:#fff;height:auto;overflow:visible}
html.js .deck,.deck{position:static;display:block;padding:0}
.slide,html.js .slide{display:flex!important;position:relative;left:auto;top:auto;transform:none;box-shadow:none;margin:0;
break-after:page;page-break-after:always}
.hud,html.js .hud{display:none}
*{-webkit-print-color-adjust:exact;print-color-adjust:exact}
}
)deck";
inline const char* const JS = R"deck(
(function(){
var d=document,root=d.documentElement,slides=[].slice.call(d.querySelectorAll('.slide')),i=0,t=0,hud=d.getElementById('hud');
root.className+=' js';
function fit(){root.style.setProperty('--fit',Math.min(window.innerWidth/1280,window.innerHeight/720));}
function show(n){i=Math.max(0,Math.min(slides.length-1,n));
for(var k=0;k<slides.length;k++){slides[k].className=slides[k].className.replace(/ on\b/g,'')+(k===i?' on':'');
slides[k].setAttribute('aria-hidden',k===i?'false':'true');}
hud.textContent=(i+1)+' / '+slides.length+'  ·  arrows to move, P to print';
hud.className='hud show';clearTimeout(t);t=setTimeout(function(){hud.className='hud';},1800);
if(history.replaceState){history.replaceState(null,'','#'+(i+1));}}
d.addEventListener('keydown',function(e){var k=e.key;
if(k==='ArrowRight'||k==='ArrowDown'||k==='PageDown'||k===' '||k==='Enter'){show(i+1);e.preventDefault();}
else if(k==='ArrowLeft'||k==='ArrowUp'||k==='PageUp'||k==='Backspace'){show(i-1);e.preventDefault();}
else if(k==='Home'){show(0);}else if(k==='End'){show(slides.length-1);}
else if((k==='p'||k==='P')&&!e.metaKey&&!e.ctrlKey){window.print();}});
d.addEventListener('click',function(e){if(e.target.closest&&e.target.closest('a')){return;}
show(e.clientX>window.innerWidth/2?i+1:i-1);});
window.addEventListener('resize',fit);
fit();var h=parseInt((location.hash||'').slice(1),10);show(h>0?h-1:0);
})();
)deck";
