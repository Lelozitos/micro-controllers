const socket = io();
const $ = (id) => document.getElementById(id);

// BCM GPIO -> physical header pin, so LEDs can be named the way they're wired.
const PHYSICAL_PIN = {
  2: 3, 3: 5, 4: 7, 5: 29, 6: 31, 7: 26, 8: 24, 9: 21, 10: 19, 11: 23, 12: 32, 13: 33,
  14: 8, 15: 10, 16: 36, 17: 11, 18: 12, 19: 35, 20: 38, 21: 40, 22: 15, 23: 16,
  24: 18, 25: 22, 26: 37, 27: 13,
};

let timerOnline = false;
let raceState = 'idle';   // idle | countdown | running | finished
let litCount = 0;
let winnerTime = null;    // race time of the winner, once the flag is out
let race = null;          // current race record from the server, or null
let savedRaces = [];       // saved races, newest first
let tagLog = [];
let clockOffset = 0;      // server clock - browser clock, ms
let cfg = { led_order: [], led_pins: [], laps: 3, checkpoints: [], spi_devices: [], racers: [] };

let focusTag = new URLSearchParams(location.search).get('focus') || store('focus');
let draftOrder = [];
let draftLines = null;    // timing line devices being edited, null = not editing
let resultsId = null;     // race shown on the results tab
let resultsRace = null;

function store(key, value) {
  try {
    if (value === undefined) return localStorage.getItem(key);
    if (value === null) localStorage.removeItem(key);
    else localStorage.setItem(key, value);
  } catch (e) { /* private mode: just don't remember */ }
  return null;
}

// ---- small DOM helper -------------------------------------------------------

function h(tag, attrs, ...children) {
  const el = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs || {})) {
    if (v == null || v === false) continue;
    if (k === 'class') el.className = v;
    else if (k.startsWith('on')) el.addEventListener(k.slice(2), v);
    else if (k in el && typeof v !== 'string') el[k] = v;
    else el.setAttribute(k, v === true ? '' : v);
  }
  for (const c of children.flat(Infinity)) {
    if (c == null || c === false) continue;
    el.append(c instanceof Node ? c : String(c));
  }
  return el;
}

// ---- time formatting --------------------------------------------------------

const DASH = '—';

// Lap and sector times: 31.234 or 1:02.345
function fmtLap(s) {
  if (s == null || !isFinite(s)) return DASH;
  if (s < 60) return s.toFixed(3);
  const m = Math.floor(s / 60);
  return `${m}:${(s - m * 60).toFixed(3).padStart(6, '0')}`;
}

// Race time: 2:41.204, 1:02:41.204
function fmtTotal(s) {
  if (s == null || !isFinite(s)) return DASH;
  const hrs = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const sec = (s % 60).toFixed(3).padStart(6, '0');
  return hrs ? `${hrs}:${String(m).padStart(2, '0')}:${sec}` : `${m}:${sec}`;
}

// Running clock with tenths: 2:41.2
function fmtClock(s) {
  s = Math.max(0, s);
  const m = Math.floor(s / 60);
  return `${m}:${(Math.floor((s - m * 60) * 10) / 10).toFixed(1).padStart(4, '0')}`;
}

function fmtGap(g) {
  if (!g) return DASH;
  if (g.laps) return `+${g.laps} lap${g.laps > 1 ? 's' : ''}`;
  return `+${g.time.toFixed(3)}`;
}

function fmtDate(ms, withYear = true) {
  return new Date(ms).toLocaleString(undefined, {
    day: 'numeric', month: 'short', year: withYear ? 'numeric' : undefined,
    hour: '2-digit', minute: '2-digit',
  });
}

function nowServer() {
  return Date.now() + clockOffset;
}

// Running or chequered flag out; an ended race (closed) is only kept to show
// its final standings.
function isLive(r) {
  return !!r && r.state !== 'closed';
}

function isUnknown(name) {
  return !name || name.startsWith('UNKNOWN');
}

function displayName(d) {
  return isUnknown(d.name) ? `Unknown ${d.tag}` : d.name;
}

function lineName(k) {
  return k === 0 ? 'Finish line' : `Sector line ${k}`;
}

// ---- race analysis ----------------------------------------------------------
//
// Works on a race record as kept by server.js: per driver, completed laps
// (with their sectors), the lap in progress, and `splits`, the race time at
// every timing line passed (P = lines passed since GO).

const EPS = 1e-6;

function analyze(r) {
  const n = r.sectors || 0;
  const L = n || 1;

  const drivers = Object.values(r.drivers).map((d) => {
    const ps = Object.keys(d.splits).map(Number);
    const P = ps.length ? Math.max(...ps) : 0;
    const done = d.laps.length;
    const lastLap = done ? d.laps[done - 1] : null;

    let best = null;
    for (const l of d.laps) if (!best || l.time < best.time) best = l;

    // Personal best per sector, counting the lap in progress too.
    const pbSec = Array(n).fill(null);
    const consider = (sectors, lap) => sectors.forEach((v, i) => {
      if (i < n && v != null && (!pbSec[i] || v < pbSec[i].time)) pbSec[i] = { time: v, lap };
    });
    for (const l of d.laps) consider(l.sectors || [], l.lap);
    consider(d.current.sectors || [], d.current.lap);

    const total = lastLap ? lastLap.elapsed : null;
    return {
      ...d,
      P,
      at: ps.length ? d.splits[P] : 0,
      done,
      lastLap,
      best,
      pbSec,
      total,
      avg: done ? d.laps.reduce((a, l) => a + l.time, 0) / done : null,
      ideal: n && pbSec.every(Boolean) ? pbSec.reduce((a, s) => a + s.time, 0) : null,
      lapStart: lastLap ? lastLap.elapsed : (d.start_s || 0),
      finished: d.place != null,
    };
  });

  // Classified drivers by place, then whoever is furthest round the track,
  // then whoever got there first.
  drivers.sort((a, b) => {
    if (a.place && b.place) return a.place - b.place;
    if (a.place || b.place) return a.place ? -1 : 1;
    if (a.P !== b.P) return b.P - a.P;
    return a.at - b.at;
  });

  // Gap to `ref` at the last line this driver passed.
  const gapTo = (d, ref) => {
    if (!ref || ref === d || d.splits[d.P] == null) return null;
    const behind = Math.floor((ref.P - d.P) / L);
    if (behind >= 1) return { laps: behind };
    const t = ref.splits[d.P];
    return t != null ? { time: Math.max(0, d.at - t) } : null;
  };
  drivers.forEach((d, i) => {
    d.pos = i + 1;
    d.gap = gapTo(d, drivers[0]);
    d.interval = gapTo(d, drivers[i - 1]);
  });

  let fastest = null;
  for (const d of drivers) {
    if (d.best && (!fastest || d.best.time < fastest.time)) {
      fastest = { time: d.best.time, lap: d.best.lap, tag: d.tag, name: displayName(d) };
    }
  }

  const bestSec = Array(n).fill(null);
  for (const d of drivers) {
    d.pbSec.forEach((s, i) => {
      if (s && (!bestSec[i] || s.time < bestSec[i].time)) {
        bestSec[i] = { ...s, tag: d.tag, name: displayName(d) };
      }
    });
  }
  const ideal = n && bestSec.every(Boolean) ? bestSec.reduce((a, s) => a + s.time, 0) : null;

  return { drivers, fastest, bestSec, ideal, n, L };
}

// purple = fastest of anyone, green = this driver's best
function timeClass(v, personal, overall) {
  if (v == null) return '';
  if (overall != null && v <= overall + EPS) return 'overall';
  if (personal != null && v <= personal + EPS) return 'personal';
  return '';
}

// ---- tabs -------------------------------------------------------------------

function showTab(name) {
  if (!$(`tab-${name}`)) name = 'live';
  document.querySelectorAll('#tabs button').forEach((b) => b.classList.toggle('active', b.dataset.tab === name));
  document.querySelectorAll('.tab-page').forEach((p) => { p.hidden = p.id !== `tab-${name}`; });
  store('tab', name);
  if (location.hash !== `#${name}`) history.replaceState(null, '', `#${name}`);
  if (name === 'results') loadResults();
}

window.addEventListener('hashchange', () => showTab(location.hash.slice(1)));

$('tabs').addEventListener('click', (e) => {
  const b = e.target.closest('button[data-tab]');
  if (b) showTab(b.dataset.tab);
});

// ---- race strip -------------------------------------------------------------

function renderLights() {
  const count = cfg.led_pins.length || 3;
  const el = $('lights');
  if (el.children.length !== count) {
    el.replaceChildren(...Array.from({ length: count }, () => h('span')));
  }
  [...el.children].forEach((d, i) => d.classList.toggle('on', raceState === 'countdown' && i < litCount));
}

function renderStrip() {
  const strip = $('race-strip');
  strip.className = `race-strip no-print state-${raceState}`;
  $('state-label').textContent = {
    idle: 'Ready', countdown: 'Starting', running: 'Race running', finished: 'Chequered flag',
  }[raceState] || raceState;

  const laps = isLive(race) ? race.laps : cfg.laps;
  let lap = DASH;
  if (race && (raceState === 'running' || raceState === 'finished')) {
    const lead = race ? analyze(race).drivers[0] : null;
    lap = raceState === 'finished' ? `${laps} / ${laps}` : `${Math.min((lead ? lead.done : 0) + 1, laps)} / ${laps}`;
  } else {
    lap = `${DASH} / ${laps}`;
  }
  $('lap-counter').textContent = lap;

  const idle = timerOnline && raceState === 'idle';
  $('btn-start').disabled = !idle;
  $('btn-reset').disabled = !timerOnline;
  $('timer-warning').hidden = timerOnline || !socket.connected;
  renderLights();
  tick();
}

function tick() {
  if (raceState === 'running' && race) {
    $('clock').textContent = fmtClock((nowServer() - race.started_at) / 1000);
  } else if (raceState === 'finished' && winnerTime != null) {
    $('clock').textContent = fmtClock(winnerTime);
  } else if (raceState !== 'finished') {
    $('clock').textContent = fmtClock(0);
  }
  const cur = document.querySelector('[data-lap-start]');
  if (cur && race) {
    const start = Number(cur.dataset.lapStart);
    cur.textContent = fmtClock((nowServer() - race.started_at) / 1000 - start);
  }
}
setInterval(tick, 100);

$('btn-start').addEventListener('click', () => socket.emit('command', { cmd: 'start' }));
$('btn-reset').addEventListener('click', () => {
  if (raceState === 'running' && !confirm('Reset ends the race in progress. Continue?')) return;
  socket.emit('command', { cmd: 'reset' });
});

// ---- live standings ---------------------------------------------------------

function sectorCells(d, a) {
  const cells = [];
  for (let i = 0; i < a.n; i++) {
    const cur = d.current.sectors ? d.current.sectors[i] : null;
    const prev = d.lastLap && d.lastLap.sectors ? d.lastLap.sectors[i] : null;
    const v = cur != null ? cur : prev;
    const cls = cur != null
      ? timeClass(v, d.pbSec[i] && d.pbSec[i].time, a.bestSec[i] && a.bestSec[i].time)
      : 'stale';
    cells.push(h('td', { class: `num t ${cls}` }, v != null ? fmtLap(v) : ''));
  }
  return cells;
}

function renderStandings() {
  const table = $('standings');
  const tbody = table.tBodies[0];
  const a = race ? analyze(race) : null;
  const list = a ? a.drivers : [];
  $('standings-empty').hidden = list.length > 0;
  table.hidden = list.length === 0;
  $('standings-caption').hidden = !(race && race.state === 'closed' && list.length);
  if (!a) { tbody.replaceChildren(); return; }

  const head = h('tr', null,
    h('th', null, 'Pos'), h('th', null, 'Driver'), h('th', { class: 'num' }, 'Laps'),
    h('th', { class: 'num' }, 'Gap'), h('th', { class: 'num' }, 'Int'),
    h('th', { class: 'num' }, 'Last lap'), h('th', { class: 'num' }, 'Best lap'),
    Array.from({ length: a.n }, (_, i) => h('th', { class: 'num' }, `S${i + 1}`)),
  );
  table.tHead.replaceChildren(head);

  const fastest = a.fastest && a.fastest.time;
  tbody.replaceChildren(...list.map((d) => h('tr', {
    class: d.tag === focusTag ? 'selected' : null,
    onclick: () => setFocus(d.tag === focusTag ? null : d.tag),
  },
    h('td', { class: 'pos' }, d.pos),
    h('td', null,
      h('span', { class: isUnknown(d.name) ? 'unknown' : 'driver-name' }, displayName(d)),
      d.finished ? h('span', { class: 'chk', title: `Classified P${d.place}` }) : null),
    h('td', { class: 'num' }, d.done),
    h('td', { class: 'num t' }, d.pos === 1 ? '' : fmtGap(d.gap)),
    h('td', { class: 'num t' }, d.pos === 1 ? '' : fmtGap(d.interval)),
    h('td', { class: `num t ${d.lastLap ? timeClass(d.lastLap.time, d.best.time, fastest) : ''}` },
      d.lastLap ? fmtLap(d.lastLap.time) : DASH),
    h('td', { class: `num t ${d.best ? timeClass(d.best.time, null, fastest) : ''}` },
      d.best ? fmtLap(d.best.time) : DASH),
    sectorCells(d, a),
  )));
}

// ---- focused driver ---------------------------------------------------------

function setFocus(tag) {
  focusTag = tag;
  store('focus', tag);
  renderStandings();
  renderFocus();
}

function stat(label, value, cls, note) {
  return h('div', null,
    h('span', { class: 'label' }, label),
    h('div', { class: `value ${cls || ''}` }, value, note ? h('span', { class: 'sub' }, note) : null));
}

function renderFocus() {
  const panel = $('focus');
  const a = race ? analyze(race) : null;
  const d = a && a.drivers.find((x) => x.tag === focusTag);
  if (!d) {
    panel.replaceChildren(h('p', { class: 'empty' },
      race && Object.keys(race.drivers).length
        ? 'Select a driver in the table to follow their laps and sectors.'
        : 'Lap and sector times for one driver show here once the race is under way.'));
    return;
  }

  const fastest = a.fastest && a.fastest.time;
  const onTrack = !d.finished && race.state !== 'closed';

  // Lap in progress: live timer + its sectors so far.
  let current;
  if (onTrack) {
    current = h('div', { class: 'focus-current' },
      h('span', { class: 'label' }, `Lap ${d.current.lap}`),
      h('div', { class: 't-big', 'data-lap-start': String(d.lapStart) }, fmtClock(0)),
      a.n ? h('div', { class: 'sector-bar' }, Array.from({ length: a.n }, (_, i) => {
        const v = d.current.sectors[i];
        const cls = v == null ? '' : timeClass(v, d.pbSec[i] && d.pbSec[i].time, a.bestSec[i] && a.bestSec[i].time) || 'done';
        return h('div', { class: cls }, h('span', { class: 'label' }, `S${i + 1}`),
          h('span', { class: `t ${cls === 'done' ? '' : cls}` }, v != null ? fmtLap(v) : DASH));
      })) : null);
  } else {
    current = h('div', { class: 'focus-current' },
      h('span', { class: 'label' }, d.finished ? `Finished P${d.place}` : 'Race over'),
      h('div', { class: 't-big' }, fmtTotal(d.total)));
  }

  const laps = [...d.laps].reverse();
  const table = h('table', { class: 'timing' },
    h('thead', null, h('tr', null,
      h('th', null, 'Lap'),
      Array.from({ length: a.n }, (_, i) => h('th', { class: 'num' }, `S${i + 1}`)),
      h('th', { class: 'num' }, 'Time'), h('th', { class: 'num' }, 'Δ best'))),
    h('tbody', null, laps.map((l) => h('tr', null,
      h('td', null, l.lap),
      Array.from({ length: a.n }, (_, i) => {
        const v = l.sectors ? l.sectors[i] : null;
        return h('td', { class: `num t ${timeClass(v, d.pbSec[i] && d.pbSec[i].time, a.bestSec[i] && a.bestSec[i].time)}` },
          v != null ? fmtLap(v) : DASH);
      }),
      h('td', { class: `num t ${timeClass(l.time, d.best.time, fastest)}` }, fmtLap(l.time)),
      h('td', { class: 'num t dim' }, l === d.best ? '' : `+${(l.time - d.best.time).toFixed(3)}`),
    ))));

  panel.replaceChildren(
    h('div', { class: 'focus-head' },
      h('span', { class: 'focus-pos' }, `P${d.pos}`),
      h('span', { class: 'focus-name' }, displayName(d)),
      h('span', { class: 'focus-tag' }, d.tag),
      h('button', { class: 'focus-close', title: 'Stop following', onclick: () => setFocus(null) }, '×')),
    current,
    h('div', { class: 'stats' },
      stat('Last lap', d.lastLap ? fmtLap(d.lastLap.time) : DASH,
        d.lastLap ? timeClass(d.lastLap.time, d.best.time, fastest) : ''),
      stat('Best lap', d.best ? fmtLap(d.best.time) : DASH,
        d.best ? timeClass(d.best.time, null, fastest) : '', d.best ? `lap ${d.best.lap}` : null),
      stat(a.n ? 'Ideal lap' : 'Average lap', a.n ? fmtLap(d.ideal) : fmtLap(d.avg)),
      stat('Gap to leader', d.pos === 1 ? 'Leader' : fmtGap(d.gap)),
      stat('Laps', `${d.done} / ${race.laps}`),
      stat('Race time', fmtTotal(d.total)),
    ),
    d.laps.length ? table : h('p', { class: 'empty' }, 'No completed laps yet.'),
  );
  tick();
}

// ---- results / print --------------------------------------------------------

function renderResultsPicker() {
  const sel = $('results-race');
  const options = [];
  const live = isLive(race);
  if (live) options.push({ id: race.id, label: `Current race · started ${fmtDate(race.started_at, false)}` });
  for (const r of savedRaces) {
    if (live && r.id === race.id) continue;
    const winner = r.winner ? ` · won by ${r.winner}` : '';
    options.push({ id: r.id, label: `${fmtDate(r.started_at)} · ${r.laps} lap${r.laps === 1 ? '' : 's'} · ${r.drivers} driver${r.drivers === 1 ? '' : 's'}${winner}` });
  }
  if (!options.some((o) => o.id === resultsId)) resultsId = options.length ? options[0].id : null;
  sel.replaceChildren(...(options.length
    ? options.map((o) => h('option', { value: o.id, selected: o.id === resultsId }, o.label))
    : [h('option', { value: '' }, 'No races yet')]));
  sel.disabled = !options.length;
  $('btn-print').disabled = !options.length;
  $('btn-delete-race').disabled = !resultsId || (live && race.id === resultsId);
}

function loadResults() {
  renderResultsPicker();
  if (!resultsId) { resultsRace = null; renderSheet(); return; }
  if (race && race.id === resultsId) { resultsRace = race; renderSheet(); return; }
  if (resultsRace && resultsRace.id === resultsId) { renderSheet(); return; }
  const id = resultsId;
  socket.emit('get_race', id, (r) => {
    if (id !== resultsId) return;
    resultsRace = r;
    renderSheet();
  });
}

$('results-race').addEventListener('change', (e) => { resultsId = e.target.value; loadResults(); });
$('results-layout').value = store('results-layout') === 'report' ? 'report' : 'tiles';
$('results-layout').addEventListener('change', (e) => {
  store('results-layout', e.target.value);
  renderSheet(); // the preview shows what will print
});
$('btn-print').addEventListener('click', () => window.print());
$('btn-csv').addEventListener('click', downloadCsv);
window.addEventListener('beforeprint', buildPrint);
window.addEventListener('afterprint', () => document.body.classList.remove('print-results'));
$('btn-delete-race').addEventListener('click', () => {
  if (!resultsId || !confirm('Delete this saved race? This cannot be undone.')) return;
  socket.emit('delete_race', resultsId);
  resultsId = null;
  resultsRace = null;
});

// Position as printed: finishing place, NC once the flag is out, else the
// running order.
function posLabel(r, d) {
  return d.place || (r.state === 'closed' || r.state === 'finished' ? 'NC' : d.pos);
}

// Gaps on the sheet are to the winner (or the leader while provisional).
function gapToWinner(a, d) {
  const ref = a.drivers.find((x) => x.place === 1) || a.drivers[0];
  if (d === ref || !d.total) return '';
  if (d.done < ref.done) {
    const n = ref.done - d.done;
    return `+${n} lap${n > 1 ? 's' : ''}`;
  }
  return `+${(d.total - ref.total).toFixed(3)}`;
}

function renderSheet() {
  const sheet = $('sheet');
  const r = resultsRace;
  $('btn-csv').disabled = !r;
  if (!r) {
    sheet.replaceChildren(h('p', { class: 'empty' }, 'No race to show yet. Results appear here after the first crossing.'));
    updatePrintHint();
    return;
  }
  const a = analyze(r);
  const closed = r.state === 'closed';
  const provisional = r.state === 'running';
  const winner = a.drivers.find((d) => d.place === 1);
  const fastest = a.fastest && a.fastest.time;
  const secHeads = () => Array.from({ length: a.n }, (_, i) => h('th', { class: 'num' }, `S${i + 1}`));

  const classification = h('table', { class: 'classification' },
    h('thead', null, h('tr', null,
      h('th', null, 'Pos'), h('th', null, 'Driver'), h('th', { class: 'num' }, 'Laps'),
      h('th', { class: 'num' }, 'Race time'), h('th', { class: 'num' }, 'Gap'),
      h('th', { class: 'num' }, 'Best lap'), h('th', { class: 'num' }, 'On lap'),
      h('th', { class: 'num' }, 'Avg lap'),
      a.n ? h('th', { class: 'num' }, 'Ideal lap') : null)),
    h('tbody', null, a.drivers.map((d) => h('tr', null,
      h('td', { class: 'pos' }, posLabel(r, d)),
      h('td', null, h('span', { class: isUnknown(d.name) ? 'unknown' : 'driver-name' }, displayName(d))),
      h('td', { class: 'num' }, d.done),
      h('td', { class: 'num t' }, fmtTotal(d.total)),
      h('td', { class: 'num t' }, gapToWinner(a, d)),
      h('td', { class: `num t ${d.best && d.best.time <= fastest + EPS ? 'overall best' : ''}` },
        d.best ? fmtLap(d.best.time) : DASH,
        d.best && d.best.time <= fastest + EPS ? h('span', { class: 'mark' }, 'FL') : null),
      h('td', { class: 'num' }, d.best ? d.best.lap : DASH),
      h('td', { class: 'num t' }, fmtLap(d.avg)),
      a.n ? h('td', { class: 'num t' }, fmtLap(d.ideal)) : null,
    ))));

  const record = (label, value, who) => h('div', { class: 'record' },
    h('div', { class: 'label' }, label),
    h('div', { class: 'value' }, value),
    who ? h('div', { class: 'who' }, who) : null);

  const records = h('div', { class: 'records' },
    record('Fastest lap', a.fastest ? fmtLap(a.fastest.time) : DASH,
      a.fastest ? `${a.fastest.name}, lap ${a.fastest.lap}` : null),
    a.bestSec.map((s, i) => record(`Best sector ${i + 1}`, s ? fmtLap(s.time) : DASH,
      s ? `${s.name}, lap ${s.lap}` : null)),
    a.n ? record('Ideal lap', fmtLap(a.ideal), 'best sectors combined') : null,
    winner ? record('Winner', fmtTotal(winner.total), `${displayName(winner)}, ${winner.done} laps`) : null,
  );

  let lapByLap = null;
  // Lap-by-lap only in the full report; the repeated copies leave it out.
  if ($('results-layout').value === 'report') {
    lapByLap = [h('h3', { class: 'lap-by-lap' }, 'Lap by lap'), a.drivers.map((d) => h('div', { class: 'driver-block lap-by-lap' },
      h('h4', null, `${d.place || (closed ? 'NC' : d.pos)}. ${displayName(d)}`,
        h('span', { class: 'sub' }, d.best ? `best ${fmtLap(d.best.time)} on lap ${d.best.lap}` : 'no completed laps')),
      d.laps.length ? h('table', null,
        h('thead', null, h('tr', null, h('th', null, 'Lap'), secHeads(),
          h('th', { class: 'num' }, 'Lap time'), h('th', { class: 'num' }, 'Δ best'),
          h('th', { class: 'num' }, 'Race time'))),
        h('tbody', null, d.laps.map((l) => {
          const isBest = l === d.best;
          const isFastest = isBest && l.time <= fastest + EPS;
          return h('tr', null,
            h('td', null, l.lap),
            Array.from({ length: a.n }, (_, i) => {
              const v = l.sectors ? l.sectors[i] : null;
              return h('td', { class: `num t ${timeClass(v, d.pbSec[i] && d.pbSec[i].time, a.bestSec[i] && a.bestSec[i].time)}` },
                v != null ? fmtLap(v) : DASH);
            }),
            h('td', { class: `num t ${isBest ? 'best' : ''} ${isFastest ? 'overall' : ''}` }, fmtLap(l.time),
              isFastest ? h('span', { class: 'mark' }, 'FL') : isBest ? h('span', { class: 'mark' }, 'PB') : null),
            h('td', { class: 'num t' }, isBest ? '' : `+${(l.time - d.best.time).toFixed(3)}`),
            h('td', { class: 'num t' }, fmtTotal(l.elapsed)));
        }))) : null))];
  }

  const metaBits = [`${r.laps} lap${r.laps > 1 ? 's' : ''}`];
  metaBits.push(a.n ? `${a.n} sectors` : 'finish line only');
  metaBits.push(`${a.drivers.length} driver${a.drivers.length === 1 ? '' : 's'}`);

  sheet.replaceChildren(...[
    h('header', null,
      h('div', { class: 'title-block' },
        h('img', { class: 'logo', src: 'logo.svg', alt: '' }),
        h('div', null,
          h('h1', null, provisional ? 'Provisional results' : 'Race results'),
          h('div', { class: 'dim' }, provisional ? 'Race in progress' : r.state === 'finished' ? 'Chequered flag shown, classification in progress' : 'Final classification'))),
      h('div', { class: 'meta' }, h('div', null, fmtDate(r.started_at)), h('div', null, metaBits.join(' · ')))),
    a.drivers.length ? [
      h('h3', null, 'Classification'), classification,
      h('h3', null, 'Records'), records,
      lapByLap,
    ] : h('p', { class: 'empty' }, 'No crossings were recorded in this race.'),
    h('div', { class: 'foot' },
      `FL = fastest lap of the race · PB = driver's best lap · NC = not classified · printed ${fmtDate(Date.now())}`),
  ].flat(Infinity).filter(Boolean));
  updatePrintHint();
}

// ---- printing ---------------------------------------------------------------
//
// "tiles": the results sheet without the lap-by-lap tables (title, date,
// classification, records, legend), as many copies as fit on one A4 page, to
// cut out and hand round. "report": the whole sheet. Either way the copy is
// built in #print-area and only that is printed.

const A4_CONTENT_MM = 297 - 2 * 10;

let mmPx = 0;
function mm(n) {
  if (!mmPx) {
    const probe = h('div', { style: 'position:absolute;visibility:hidden;height:100mm' });
    document.body.append(probe);
    mmPx = probe.getBoundingClientRect().height / 100;
    probe.remove();
  }
  return n * mmPx;
}

function tile() {
  const copy = $('sheet').cloneNode(true);
  copy.querySelectorAll('.lap-by-lap').forEach((el) => el.remove());
  return h('div', { class: 'tile' }, ...copy.childNodes);
}

// Copies of the results that fit on one page.
function tileCount() {
  const area = $('print-area');
  area.className = 'print-area doc tiles';
  area.replaceChildren(tile(), tile());
  // Second tile = a typical one (with its top padding).
  const one = area.children[1].getBoundingClientRect().height;
  const first = area.children[0].getBoundingClientRect().height;
  area.replaceChildren();
  if (!one) return 1;
  return Math.max(1, 1 + Math.floor((mm(A4_CONTENT_MM) - first - 1) / one));
}

function buildPrint() {
  const resultsShown = !$('tab-results').hidden && resultsRace;
  document.body.classList.toggle('print-results', !!resultsShown);
  if (!resultsShown) return;
  const area = $('print-area');
  const table = $('sheet').querySelector('table.classification');

  if ($('results-layout').value === 'tiles' && table) {
    const n = tileCount();
    area.className = 'print-area doc tiles';
    area.replaceChildren(...Array.from({ length: n }, () => tile()));
    return;
  }

  // Full report. The empty header/footer rows of a table repeat on every
  // printed page, giving each page a top and bottom margin without the
  // browser's own header and footer.
  const sheet = $('sheet').cloneNode(true);
  sheet.removeAttribute('id');
  area.className = 'print-area doc report';
  area.replaceChildren(h('table', { class: 'print-frame' },
    h('thead', null, h('tr', null, h('td'))),
    h('tfoot', null, h('tr', null, h('td'))),
    h('tbody', null, h('tr', null, h('td', null, sheet)))));
}

function updatePrintHint() {
  const hint = $('results-hint');
  const saved = 'Every race is saved automatically on the Pi (in dashboard/results) and stays in the list above until you delete it.';
  const table = $('sheet').querySelector('table.classification');
  if ($('results-layout').value === 'tiles' && table && !$('tab-results').hidden) {
    const n = tileCount();
    hint.textContent = `${saved} Printing fits ${n} cop${n === 1 ? 'y' : 'ies'} of the results on one A4 page.`;
  } else {
    hint.textContent = saved;
  }
}

// ---- CSV download -----------------------------------------------------------

function downloadCsv() {
  const r = resultsRace;
  if (!r) return;
  const a = analyze(r);
  const t = (v) => (v == null ? '' : fmtLap(v));
  const rows = [
    ['Race', fmtDate(r.started_at)],
    ['Laps', r.laps],
    ['Sectors', a.n],
    ['Fastest lap', a.fastest ? fmtLap(a.fastest.time) : '', a.fastest ? a.fastest.name : '', a.fastest ? `lap ${a.fastest.lap}` : ''],
    [],
    ['Pos', 'Driver', 'Tag', 'Laps', 'Race time', 'Gap', 'Best lap', 'Best lap on lap', 'Avg lap', ...(a.n ? ['Ideal lap'] : [])],
    ...a.drivers.map((d) => [posLabel(r, d), displayName(d), d.tag, d.done, d.total == null ? '' : fmtTotal(d.total),
      gapToWinner(a, d), d.best ? fmtLap(d.best.time) : '', d.best ? d.best.lap : '', t(d.avg), ...(a.n ? [t(d.ideal)] : [])]),
    [],
    ['Driver', 'Lap', ...Array.from({ length: a.n }, (_, i) => `S${i + 1}`), 'Lap time', 'Race time'],
    ...a.drivers.flatMap((d) => d.laps.map((l) => [displayName(d), l.lap,
      ...Array.from({ length: a.n }, (_, i) => t(l.sectors ? l.sectors[i] : null)), fmtLap(l.time), fmtTotal(l.elapsed)])),
  ];
  const cell = (v) => {
    const s = String(v ?? '');
    return /[",\n]/.test(s) ? `"${s.replace(/"/g, '""')}"` : s;
  };
  const csv = rows.map((row) => row.map(cell).join(',')).join('\r\n');
  const d = new Date(r.started_at);
  const pad = (n) => String(n).padStart(2, '0');
  const name = `race-${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}-${pad(d.getHours())}${pad(d.getMinutes())}.csv`;
  const url = URL.createObjectURL(new Blob(['\ufeff' + csv], { type: 'text/csv' }));
  const link = h('a', { href: url, download: name });
  document.body.append(link);
  link.click();
  link.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

// ---- setup: laps ------------------------------------------------------------

function sendLaps(n) {
  const laps = Math.max(1, Math.min(99, Math.round(Number(n)) || 1));
  $('laps-input').value = laps;
  socket.emit('command', { cmd: 'set_laps', laps });
}

$('btn-laps-minus').addEventListener('click', () => sendLaps(Number($('laps-input').value) - 1));
$('btn-laps-plus').addEventListener('click', () => sendLaps(Number($('laps-input').value) + 1));
$('laps-input').addEventListener('change', () => sendLaps($('laps-input').value));

// ---- setup: timing lines ----------------------------------------------------

function savedLines() {
  return cfg.checkpoints.map((c) => c.device);
}

function lines() {
  return draftLines || savedLines();
}

function editLines(fn) {
  const next = [...lines()];
  fn(next);
  draftLines = next.join() === savedLines().join() ? null : next;
  renderLines();
  renderSim();
}

function renderLines() {
  const editable = timerOnline && raceState === 'idle';
  const list = lines();
  const saved = cfg.checkpoints;
  const tbody = $('lines-table').tBodies[0];

  tbody.replaceChildren(...list.map((dev, k) => {
    const options = [...new Set([...cfg.spi_devices, dev].filter(Boolean))];
    let status;
    if (saved[k] && saved[k].device === dev) {
      status = saved[k].ok ? h('span', { class: 'personal' }, 'Reader OK') : h('span', { class: 'unknown' }, 'Not found');
    } else {
      status = h('span', { class: 'dim' }, 'Not saved');
    }
    return h('tr', null,
      h('td', null, lineName(k)),
      h('td', null, h('select', {
        disabled: !editable,
        onchange: (e) => editLines((l) => {
          const other = l.indexOf(e.target.value);
          if (other >= 0) l[other] = l[k]; // swap if taken by another line
          l[k] = e.target.value;
        }),
      }, options.map((o) => h('option', { value: o, selected: o === dev }, o)))),
      h('td', null, status),
      h('td', { class: 'num' }, k > 0
        ? h('button', { class: 'btn btn-small', disabled: !editable, onclick: () => editLines((l) => l.splice(k, 1)) }, 'Remove')
        : null));
  }));

  const n = list.length;
  $('lines-summary').textContent = n <= 1
    ? 'Finish line only: laps are timed, no sectors.'
    : `${n} sectors: ` + Array.from({ length: n }, (_, i) =>
      `S${i + 1} ${i === 0 ? 'finish' : `line ${i}`} → ${i === n - 1 ? 'finish' : `line ${i + 1}`}`).join(', ') + '.';

  const free = cfg.spi_devices.filter((d) => !list.includes(d));
  const add = $('btn-line-add');
  add.disabled = !editable || !free.length || n >= 6;
  add.title = free.length ? '' : 'No unused SPI device. Enable more chip selects on the Pi (see README).';
  $('btn-lines-save').disabled = !editable || !draftLines;
  $('btn-lines-revert').disabled = !draftLines;
}

$('btn-line-add').addEventListener('click', () => editLines((l) => {
  const free = cfg.spi_devices.find((d) => !l.includes(d));
  if (free) l.push(free);
}));
$('btn-lines-revert').addEventListener('click', () => { draftLines = null; renderLines(); renderSim(); });
$('btn-lines-save').addEventListener('click', () => {
  socket.emit('command', { cmd: 'set_checkpoints', devices: lines() });
});

// ---- setup: drivers ---------------------------------------------------------
//
// Edited as a draft and sent to the timer as a whole, which rewrites
// racers.csv. Inputs update the draft without re-rendering, to keep focus.

let draftRacers = null;   // [{ tag, name }] being edited, null = not editing
let scanRow = null;       // row waiting for the next tag read
let pendingRacers = null; // list sent to the timer, until it confirms
let pendingTimer = null;
let statusTimer = null;

const sameRacers = (a, b) => JSON.stringify(a) === JSON.stringify(b);
const byTag = (list) => list.map((r) => ({ tag: r.tag, name: r.name })).sort((x, y) => x.tag.localeCompare(y.tag));

function racersStatus(text, kind) {
  const el = $('racers-status');
  clearTimeout(statusTimer);
  el.textContent = text;
  el.className = `save-status ${kind || ''}`;
  el.hidden = false;
  if (kind === 'ok') statusTimer = setTimeout(() => { el.hidden = true; }, 5000);
}

// The timer answers a save by republishing its config with the roster it
// actually has; that's the confirmation that racers.csv was written.
function checkRacersSaved() {
  if (!pendingRacers || !sameRacers(byTag(pendingRacers), byTag(cfg.racers))) return;
  clearTimeout(pendingTimer);
  const n = pendingRacers.length;
  pendingRacers = null;
  racersStatus(`Saved ${n} driver${n === 1 ? '' : 's'} to racers.csv`, 'ok');
  updateRacerButtons();
}
const racersList = () => draftRacers || cfg.racers;

function editRacers(fn) {
  const next = racersList().map((r) => ({ ...r }));
  fn(next);
  draftRacers = next;
  renderRacers(true);
}

// First problem with the draft, or null if it can be saved.
function racersProblem(list) {
  const seen = new Set();
  for (const [i, r] of list.entries()) {
    const tag = r.tag.trim().toUpperCase();
    if (!/^[0-9A-F]{1,20}$/.test(tag)) return { row: i, field: 'tag', text: `Row ${i + 1}: the tag ID must be hex digits (0-9, A-F), e.g. A1B2C3D4.` };
    if (seen.has(tag)) return { row: i, field: 'tag', text: `Row ${i + 1}: tag ${tag} is already used by another driver.` };
    seen.add(tag);
    if (!r.name.trim()) return { row: i, field: 'name', text: `Row ${i + 1}: enter a name.` };
  }
  return null;
}

function updateRacerButtons() {
  const list = racersList();
  const problem = draftRacers ? racersProblem(list) : null;
  document.querySelectorAll('#racers-table input').forEach((inp) => {
    inp.classList.toggle('invalid', !!problem && Number(inp.dataset.row) === problem.row && inp.dataset.field === problem.field);
  });
  $('racers-error').hidden = !problem;
  $('racers-error').textContent = problem ? problem.text : '';
  const changed = draftRacers && !sameRacers(draftRacers, cfg.racers);
  $('btn-racers-save').disabled = !timerOnline || !changed || !!problem || !!pendingRacers;
  $('btn-racers-revert').disabled = !draftRacers;
}

function renderRacers(force) {
  // Status updates arrive all the time; don't rebuild under someone typing.
  if (!force && document.activeElement && document.activeElement.matches('#racers-table input')) {
    updateRacerButtons();
    return;
  }
  const list = racersList();
  const tbody = $('racers-table').tBodies[0];
  tbody.replaceChildren(...(list.length ? list.map((r, i) => h('tr', null,
    h('td', { class: 'tag-cell' }, h('input', {
      type: 'text', class: 'tag-input', value: r.tag, maxlength: '20', placeholder: 'A1B2C3D4',
      'data-row': String(i), 'data-field': 'tag',
      oninput: (e) => { if (!draftRacers) draftRacers = racersList().map((x) => ({ ...x })); draftRacers[i].tag = e.target.value; updateRacerButtons(); },
    }), ' ', h('button', {
      class: `btn btn-small ${scanRow === i ? 'scanning' : ''}`,
      title: 'Fill in the tag ID from the next tag held to any reader',
      onclick: () => { scanRow = scanRow === i ? null : i; renderRacers(true); },
    }, scanRow === i ? 'Hold tag to a reader…' : 'Scan')),
    h('td', null, h('input', {
      type: 'text', value: r.name, maxlength: '40', placeholder: 'Driver name',
      'data-row': String(i), 'data-field': 'name',
      oninput: (e) => { if (!draftRacers) draftRacers = racersList().map((x) => ({ ...x })); draftRacers[i].name = e.target.value; updateRacerButtons(); },
    })),
    h('td', { class: 'num' }, h('button', {
      class: 'btn btn-small', title: 'Remove this driver',
      onclick: () => { if (scanRow === i) scanRow = null; editRacers((l) => l.splice(i, 1)); },
    }, 'Remove'))))
    : [h('tr', null, h('td', { colspan: 4, class: 'dim' }, 'No drivers yet. Add one, or hold a tag to a reader and add it from the list below.'))]));

  // Tags seen at a reader that nobody has: one click to add them.
  const known = new Set(list.map((r) => r.tag.trim().toUpperCase()));
  const unknown = [...new Set(tagLog.map((t) => t.tag))].filter((t) => !known.has(t));
  const box = $('unknown-tags');
  box.hidden = !unknown.length;
  box.replaceChildren('Unregistered tags seen:', ...unknown.map((tag) => h('button', {
    class: 'btn btn-small', title: 'Add a driver with this tag',
    onclick: () => editRacers((l) => l.push({ tag, name: '' })),
  }, `+ ${tag}`)));

  $('btn-reload-racers').disabled = !timerOnline;
  updateRacerButtons();
}

$('btn-racer-add').addEventListener('click', () => {
  editRacers((l) => l.push({ tag: '', name: '' }));
  const inputs = document.querySelectorAll('#racers-table input.tag-input');
  if (inputs.length) inputs[inputs.length - 1].focus();
});
$('btn-racers-revert').addEventListener('click', () => { draftRacers = null; scanRow = null; renderRacers(true); });
$('btn-racers-save').addEventListener('click', () => {
  const racers = racersList().map((r) => ({ tag: r.tag.trim().toUpperCase(), name: r.name.trim() }));
  if (racersProblem(racers)) return;
  pendingRacers = racers;
  racersStatus('Saving…');
  updateRacerButtons();
  socket.emit('command', { cmd: 'set_racers', racers });
  clearTimeout(pendingTimer);
  pendingTimer = setTimeout(() => {
    if (!pendingRacers) return;
    pendingRacers = null;
    racersStatus('Not saved: the timer did not confirm. Check it is running and that racers.csv is writable.', 'err');
    updateRacerButtons();
  }, 4000);
});

$('btn-reload-racers').addEventListener('click', () => socket.emit('command', { cmd: 'reload_racers' }));

// ---- developer: start lights ------------------------------------------------

function moveLight(pos, delta) {
  const to = pos + delta;
  if (to < 0 || to >= draftOrder.length) return;
  [draftOrder[pos], draftOrder[to]] = [draftOrder[to], draftOrder[pos]];
  renderOrder();
}

function renderOrder() {
  const tbody = $('order-table').tBodies[0];
  const editable = timerOnline && raceState === 'idle';
  if (!draftOrder.length) {
    tbody.replaceChildren(h('tr', null, h('td', { colspan: 4, class: 'dim' }, 'Waiting for the race timer.')));
    return;
  }
  tbody.replaceChildren(...draftOrder.map((led, pos) => {
    const gpio = cfg.led_pins[led];
    const phys = PHYSICAL_PIN[gpio];
    return h('tr', null,
      h('td', null, pos + 1),
      h('td', null, `Light ${String.fromCharCode(65 + led)}`),
      h('td', { class: 'dim' }, `GPIO${gpio}${phys ? ` · pin ${phys}` : ''}`),
      h('td', { class: 'num' },
        h('button', { class: 'btn btn-small', title: 'Light earlier', disabled: !editable || pos === 0, onclick: () => moveLight(pos, -1) }, '↑'), ' ',
        h('button', { class: 'btn btn-small', title: 'Light later', disabled: !editable || pos === draftOrder.length - 1, onclick: () => moveLight(pos, 1) }, '↓'), ' ',
        h('button', { class: 'btn btn-small', title: 'Light this LED for 1 s', disabled: !editable, onclick: () => socket.emit('command', { cmd: 'test_led', led }) }, 'Test')));
  }));
  const changed = draftOrder.join() !== cfg.led_order.join();
  $('btn-order-save').disabled = !editable || !changed;
  $('btn-order-revert').disabled = !changed;
}

$('btn-order-save').addEventListener('click', () => socket.emit('command', { cmd: 'set_led_order', order: draftOrder }));
$('btn-order-revert').addEventListener('click', () => { draftOrder = [...cfg.led_order]; renderOrder(); });

// ---- developer: simulated reads + reader activity ---------------------------

function renderSim() {
  const lineSel = $('sim-line');
  const keepLine = lineSel.value;
  lineSel.replaceChildren(...cfg.checkpoints.map((c, k) =>
    h('option', { value: k, selected: String(k) === keepLine }, `${lineName(k)} (${c.device})`)));

  const drvSel = $('sim-driver');
  const keepDrv = drvSel.value;
  drvSel.replaceChildren(
    ...cfg.racers.map((r) => h('option', { value: r.tag, selected: r.tag === keepDrv }, `${r.name} (${r.tag})`)),
    h('option', { value: '', selected: keepDrv === '' && cfg.racers.length === 0 }, 'Other tag…'));
  $('sim-custom-row').hidden = drvSel.value !== '';
  $('btn-sim').disabled = !timerOnline || !cfg.checkpoints.length;
}

$('sim-driver').addEventListener('change', () => { $('sim-custom-row').hidden = $('sim-driver').value !== ''; });
$('btn-sim').addEventListener('click', () => {
  const tag = ($('sim-driver').value || $('sim-tag').value).trim();
  if (!/^[0-9A-Fa-f]{1,20}$/.test(tag)) { $('sim-tag').focus(); return; }
  socket.emit('command', { cmd: 'sim_tag', checkpoint: Number($('sim-line').value), tag });
});

function renderTags() {
  const tbody = $('tag-table').tBodies[0];
  tbody.replaceChildren(...(tagLog.length
    ? tagLog.map((t) => h('tr', null,
      h('td', { class: 't dim' }, new Date(t.at).toLocaleTimeString()),
      h('td', null, lineName(t.checkpoint)),
      h('td', { class: 't' }, t.tag),
      h('td', { class: isUnknown(t.racer) ? 'unknown' : '' }, isUnknown(t.racer) ? 'not in racers.csv' : t.racer)))
    : [h('tr', null, h('td', { colspan: 4, class: 'dim' }, 'No reads yet. Hold a tag to any reader.'))]));
}

// ---- socket events ----------------------------------------------------------

function renderAll() {
  renderStrip();
  renderStandings();
  renderFocus();
  renderLines();
  renderRacers();
  renderOrder();
  renderSim();
}

socket.on('connect', () => {
  $('timer-dot').className = 'status-dot';
  $('timer-label').textContent = 'connected';
});

socket.on('disconnect', () => {
  timerOnline = false;
  $('timer-dot').className = 'status-dot off';
  $('timer-label').textContent = 'dashboard disconnected';
  renderAll();
});

socket.on('server_time', (t) => { clockOffset = t - Date.now(); });

socket.on('timer', (online) => {
  timerOnline = online;
  $('timer-dot').className = `status-dot ${online ? 'on' : 'off'}`;
  $('timer-label').textContent = online ? 'Timer online' : 'Timer offline';
  renderAll();
});

socket.on('state', (data) => {
  raceState = data.state;
  litCount = data.state === 'countdown' ? data.lit || 0 : 0;
  if (data.state === 'finished') winnerTime = data.elapsed_s;
  else if (data.state !== 'countdown') winnerTime = null;
  renderAll();
});

socket.on('race', (r) => {
  race = r;
  renderStrip();
  renderStandings();
  renderFocus();
  if (!$('tab-results').hidden) {
    if (race && race.id === resultsId) resultsRace = race;
    if (!race || !resultsId || race.id === resultsId) loadResults();
    else renderResultsPicker();
  }
});

socket.on('history', (list) => {
  savedRaces = Array.isArray(list) ? list : [];
  if (!$('tab-results').hidden) loadResults();
});

socket.on('tags', (list) => { tagLog = Array.isArray(list) ? list : []; renderTags(); renderRacers(); });
socket.on('tag', (t) => {
  tagLog = [t, ...tagLog].slice(0, 50);
  renderTags();
  if (scanRow != null && racersList()[scanRow]) {
    const row = scanRow;
    scanRow = null;
    editRacers((l) => { l[row].tag = t.tag; });
  } else {
    renderRacers();
  }
});

socket.on('config', (c) => {
  const prevOrder = cfg.led_order || [];
  cfg = {
    led_order: c.led_order || [], led_pins: c.led_pins || [], laps: c.laps || 3,
    checkpoints: c.checkpoints || [], spi_devices: c.spi_devices || [],
    racers: (c.racers || []).slice().sort((x, y) => x.name.localeCompare(y.name)),
  };
  if (document.activeElement !== $('laps-input')) $('laps-input').value = cfg.laps;
  // Keep an unsaved light order unless it was just saved.
  if (!draftOrder.length || draftOrder.join() === prevOrder.join() || draftOrder.join() === cfg.led_order.join()) {
    draftOrder = [...cfg.led_order];
  }
  if (draftLines && draftLines.join() === savedLines().join()) draftLines = null;
  if (draftRacers && sameRacers(byTag(draftRacers.map((r) => ({ tag: r.tag.trim().toUpperCase(), name: r.name.trim() }))), byTag(cfg.racers))) {
    draftRacers = null;
  }
  checkRacersSaved();
  renderAll();
});

showTab(location.hash.slice(1) || store('tab') || 'live');
renderAll();
renderTags();
