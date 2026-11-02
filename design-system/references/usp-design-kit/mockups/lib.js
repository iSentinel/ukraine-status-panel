/* Mockup helpers. Geometry is computed the same way LVGL code would compute it. */
const C = {sun:'#FFC24D', moon:'#DDE5F2', cl:'#EAF0F7', clm:'#B7C4D3', cld:'#7F93A8',
           drop:'#5AA9FF', flake:'#E6F2FF', bolt:'#FFD34D', fog:'#A9B7C6', unk:'#91A8B2'};

/* Extended WMO table: code -> [ua text, icon recipe]. Recipe uses layer primitives. */
const WMO = {
  0:['Ясно','clear'], 1:['Переважно ясно','mostly'], 2:['Мінлива хмарність','partly'], 3:['Хмарно','overcast'],
  45:['Туман','fog'], 48:['Туман, паморозь','fog'],
  51:['Слабка мряка','drizzle'], 53:['Мряка','drizzle'], 55:['Сильна мряка','drizzle'],
  56:['Крижана мряка','sleet'], 57:['Крижана мряка','sleet'],
  61:['Невеликий дощ','rain'], 63:['Дощ','rain'], 65:['Сильний дощ','rainh'],
  66:['Крижаний дощ','sleet'], 67:['Сильний крижаний дощ','sleet'],
  71:['Невеликий сніг','snow'], 73:['Сніг','snow'], 75:['Сильний сніг','snow'], 77:['Снігова крупа','snow'],
  80:['Короткочасний дощ','showers'], 81:['Злива','showers'], 82:['Сильна злива','showers'],
  85:['Снігові заряди','snowshowers'], 86:['Сильні снігові заряди','snowshowers'],
  95:['Гроза','storm'], 96:['Гроза з градом','stormhail'], 99:['Сильна гроза з градом','stormhail']
};

function layers(recipe, day){
  const orb = day ? ['p-sun', C.sun] : ['p-moon', C.moon];
  switch(recipe){
    case 'clear': return [day ? ['p-sun-big', C.sun] : ['p-moon-big', C.moon]];
    case 'mostly': return [orb, ['p-cloud', C.cl, 'translate(14 10) scale(.62)']];
    case 'partly': return [orb, ['p-cloud', C.cl]];
    case 'overcast': return [['p-cloud-back', C.clm], ['p-cloud', C.cl]];
    case 'fog': return [['p-cloud-low', C.clm], ['p-fog', C.fog]];
    case 'drizzle': return [['p-cloud-low', C.cl], ['p-drops-light', C.drop]];
    case 'rain': return [['p-cloud-low', C.cl], ['p-drops', C.drop]];
    case 'rainh': return [['p-cloud-low', C.clm], ['p-drops-heavy', C.drop]];
    case 'sleet': return [['p-cloud-low', C.cl], ['p-sleet', C.flake]];
    case 'snow': return [['p-cloud-low', C.cl], ['p-flakes', C.flake]];
    case 'showers': return [[orb[0], orb[1], 'translate(0 -8)'], ['p-cloud-low', C.cl], ['p-drops', C.drop]];
    case 'snowshowers': return [[orb[0], orb[1], 'translate(0 -8)'], ['p-cloud-low', C.cl], ['p-flakes', C.flake]];
    case 'storm': return [['p-cloud-low', C.cld], ['p-bolt', C.bolt]];
    case 'stormhail': return [['p-cloud-low', C.cld], ['p-bolt', C.bolt], ['p-flakes', C.flake, 'translate(16 -2) scale(.7)']];
    default: return [['p-unknown', C.unk]];
  }
}
function icon(code, day, size, mono){
  const rec = WMO[code] ? WMO[code][1] : 'unknown';
  const ls = layers(rec, day);
  const g = ls.map(([id, col, tr]) =>
    `<use href="#${id}" width="64" height="64" style="color:${mono ? monoOf(col) : col}" ${tr ? `transform="${tr}"` : ''}/>`).join('');
  return `<svg width="${size}" height="${size}" viewBox="0 0 64 64" style="display:block;overflow:visible">${g}</svg>`;
}
function monoOf(col){ const m = {[C.sun]:'#F2F2F2',[C.moon]:'#E0E0E0',[C.cl]:'#BFBFBF',[C.clm]:'#8C8C8C',[C.cld]:'#6E6E6E',[C.drop]:'#F2F2F2',[C.flake]:'#FFFFFF',[C.bolt]:'#FFFFFF',[C.fog]:'#A6A6A6'}; return m[col] || '#CCCCCC'; }
function wtext(code){ return WMO[code] ? WMO[code][0] : 'Невідомо'; }

/* Absolute temperature colour scale (soft hues, distinct from alert semantics). */
const TSTOPS = [[-20,'#9D8CFF'],[-10,'#6F8DFF'],[-5,'#5AA8FF'],[0,'#53C6E8'],[5,'#5ED3B9'],[10,'#92D99A'],
                [15,'#CFE07A'],[20,'#F3D06A'],[25,'#F7AD62'],[30,'#F2875F'],[35,'#E86A6F']];
function hex2rgb(h){return [1,3,5].map(i=>parseInt(h.substr(i,2),16));}
function rgb2hex(a){return '#'+a.map(v=>Math.round(v).toString(16).padStart(2,'0')).join('');}
function tcol(t){
  if (t <= TSTOPS[0][0]) return TSTOPS[0][1];
  for (let i=1;i<TSTOPS.length;i++){
    if (t <= TSTOPS[i][0]){ const [t0,c0]=TSTOPS[i-1],[t1,c1]=TSTOPS[i]; const k=(t-t0)/(t1-t0);
      const a=hex2rgb(c0), b=hex2rgb(c1); return rgb2hex(a.map((v,j)=>v+(b[j]-v)*k)); }
  }
  return TSTOPS[TSTOPS.length-1][1];
}
function deg(t){ return (t<0?'−':'')+Math.abs(Math.round(t))+'°'; }

/* Shared status bar: region alert state is always visible (product priority). */
function statusBar(o){
  const st = o.alert
    ? `<div class="chip" style="left:12px;background:#3A1218;border:1px solid #7A2230"><span class="dot" style="background:var(--mis)"></span><b style="font-weight:600">${o.region}</b>&nbsp;· ${o.alert}</div>`
    : `<div class="chip" style="left:12px;background:#0E2A1B;border:1px solid #1F5A39"><span class="dot" style="background:#2FBF71"></span><b style="font-weight:600">${o.region}</b>&nbsp;· ${o.regionText||'тривоги немає'}</div>`;
  const right = `<div class="abs" style="right:14px;top:0;height:44px;display:flex;align-items:center;gap:18px">
    <span class="t18 c2">${o.date}</span>
    ${o.weather?`<span class="t18" style="display:flex;gap:6px;align-items:center">${icon(o.weather[0],o.weather[1],26)}<span class="w6">${o.weather[2]}</span></span>`:''}
    <span style="display:flex;gap:4px;align-items:center;color:${o.power?'#9BE3B6':'#F5BE26'}"><svg width="22" height="22"><use href="#g-bolt"/></svg><span class="t18">${o.power?'Є світло':'Немає світла'}</span></span>
    <span class="c2" style="display:flex"><svg width="22" height="22"><use href="#g-wifi"/></svg></span>
    <span class="t22 w6 num">${o.time}</span></div>`;
  return `<div class="sbar">${st}${right}</div>`;
}
function navBar(active){
  const items=[['n-home','Огляд'],['n-map','Карта'],['n-weather','Погода'],['n-power','Живлення'],['n-history','Історія'],['n-clock','Годинник'],['n-settings','Налаштування']];
  return `<div class="nav">${items.map(([ic,t],i)=>`<div class="it ${i===active?'on':''}"><svg><use href="#${ic}"/></svg><span>${t}</span></div>`).join('')}</div>`;
}

/* Hourly strip: N columns, temperature polyline on a local scale, POP only when >=20%. */
function hourly(el, data, o){
  const W=o.w, colW=W/data.length, top=o.top, h=o.h;
  const ts=data.map(d=>d.t), mn=Math.min(...ts), mx=Math.max(...ts), span=Math.max(4,mx-mn);
  const yOf=t=> top + h - (t-mn)/span*h;
  let svg=`<svg class="abs" style="left:0;top:0;overflow:visible" width="${W}" height="${top+h+40}">`;
  const pts=data.map((d,i)=>[colW*i+colW/2, yOf(d.t)]);
  svg+=`<polyline points="${pts.map(p=>p.join(',')).join(' ')}" fill="none" stroke="#C9D6E6" stroke-width="3" stroke-linejoin="round" stroke-linecap="round" opacity=".85"/>`;
  pts.forEach((p,i)=>{svg+=`<circle cx="${p[0]}" cy="${p[1]}" r="5" fill="${tcol(data[i].t)}" stroke="#0C1828" stroke-width="2"/>`;});
  svg+='</svg>';
  let html=svg;
  data.forEach((d,i)=>{
    const x=colW*i;
    html+=`<div class="abs t20 w6 num" style="left:${x}px;width:${colW}px;text-align:center;top:${yOf(d.t)-34}px">${deg(d.t)}</div>`;
    html+=`<div class="abs" style="left:${x+colW/2-18}px;top:${o.iconTop}px">${icon(d.c,d.day,36)}</div>`;
    html+=`<div class="abs t18 num ${i===0?'w6':''}" style="left:${x}px;width:${colW}px;text-align:center;top:${o.timeTop}px;color:${i===0?'#F8FAFC':'#91A8B2'}">${d.h}</div>`;
    if (d.p>=20) html+=`<div class="abs t18 num cp" style="left:${x}px;width:${colW}px;text-align:center;top:${o.iconTop+38}px">${d.p}%</div>`;
  });
  el.innerHTML=html;
}

/* Daily list: fixed columns, one shared scale for all rows, absolute colour mapping. */
function daily(el, days, o){
  const lo=Math.min(...days.map(d=>d.lo)), hi=Math.max(...days.map(d=>d.hi));
  const bx=o.barX, bw=o.barW;
  const xOf=t=> bx + (t-lo)/(hi-lo)*bw;
  let html='';
  days.forEach((d,i)=>{
    const y=i*o.rowH;
    if (i>0) html+=`<div class="abs" style="left:0;right:0;top:${y}px;height:1px;background:#1A2C42"></div>`;
    html+=`<div class="abs t20 ${i===0?'w6':''}" style="left:0;top:${y+(o.rowH-27)/2}px;width:${o.dayW}px">${d.d}</div>`;
    html+=`<div class="abs" style="left:${o.iconX}px;top:${y+(o.rowH-34)/2}px">${icon(d.c,true,34)}</div>`;
    if (d.p>=20) html+=`<div class="abs t18 num cp" style="left:${o.popX}px;width:52px;top:${y+(o.rowH-24)/2}px">${d.p}%</div>`;
    html+=`<div class="abs t20 num c2" style="left:${bx-62}px;width:50px;text-align:right;top:${y+(o.rowH-27)/2}px">${deg(d.lo)}</div>`;
    html+=`<div class="abs" style="left:${bx}px;width:${bw}px;top:${y+o.rowH/2-4}px;height:8px;border-radius:4px;background:#1C2E44"></div>`;
    html+=`<div class="abs" style="left:${xOf(d.lo)}px;width:${Math.max(8,xOf(d.hi)-xOf(d.lo))}px;top:${y+o.rowH/2-4}px;height:8px;border-radius:4px;background:linear-gradient(90deg,${tcol(d.lo)},${tcol(d.hi)})"></div>`;
    if (d.now!==undefined) html+=`<div class="abs" style="left:${xOf(d.now)-7}px;top:${y+o.rowH/2-7}px;width:14px;height:14px;border-radius:7px;background:#F8FAFC;border:3px solid #0C1828"></div>`;
    html+=`<div class="abs t20 w6 num" style="left:${bx+bw+12}px;width:50px;top:${y+(o.rowH-27)/2}px">${deg(d.hi)}</div>`;
  });
  el.innerHTML=html;
}
