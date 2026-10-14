const socket = io();

const connDot = document.getElementById('conn-dot');
const connLabel = document.getElementById('conn-label');
const stateCard = document.getElementById('state-card');
const stateLabel = document.getElementById('state-label');
const stateSub = document.getElementById('state-sub');
const clockEl = document.getElementById('clock');
const boardList = document.getElementById('board-list');
const emptyRow = document.getElementById('empty-row');
const lightsEl = document.getElementById('f1-lights');
const btnStart = document.getElementById('btn-start');
const btnReset = document.getElementById('btn-reset');
const timerWarning = document.getElementById('timer-warning');
const orderList = document.getElementById('order-list');
const btnOrderSave = document.getElementById('btn-order-save');
const btnOrderRevert = document.getElementById('btn-order-revert');
const lapCounter = document.getElementById('lap-counter');
const lapsInput = document.getElementById('laps-input');
const btnLapsMinus = document.getElementById('btn-laps-minus');
const btnLapsPlus = document.getElementById('btn-laps-plus');

// BCM GPIO -> physical header pin, so LEDs can be named the way they're wired.
const PHYSICAL_PIN = {
  2: 3, 3: 5, 4: 7, 5: 29, 6: 31, 7: 26, 8: 24, 9: 21, 10: 19, 11: 23, 12: 32, 13: 33,
  14: 8, 15: 10, 16: 36, 17: 11, 18: 12, 19: 35, 20: 38, 21: 40, 22: 15, 23: 16,
  24: 18, 25: 22, 26: 37, 27: 13,
};

let startedAt = null;   // epoch ms from the race timer, or null when idle
let clockTimer = null;
let raceState = 'idle'; // idle | countdown | running | finished
let raceLaps = 3;       // laps of the current race (or the setting, when idle)
let leaderLaps = 0;
let timerOnline = false;
let ledPins = [];       // countdown LED pins, by LED index
let savedOrder = [];    // order the timer is using
let draftOrder = [];    // order being edited here

function formatElapsed(seconds) {
  const m = Math.floor(seconds / 60);
  const s = seconds - m * 60;
  return `${String(m).padStart(2, '0')}:${s.toFixed(2).padStart(5, '0')}`;
}

function tickClock() {
  if (startedAt == null) return;
  const elapsed = (Date.now() - startedAt) / 1000;
  clockEl.textContent = formatElapsed(Math.max(0, elapsed));
}

// ---- start lights -------------------------------------------------------

function renderLights(lit) {
  const count = ledPins.length || 3;
  if (lightsEl.children.length !== count) {
    lightsEl.innerHTML = '';
    for (let i = 0; i < count; i++) {
      const d = document.createElement('div');
      d.className = 'f1-light';
      lightsEl.appendChild(d);
    }
  }
  [...lightsEl.children].forEach((d, i) => d.classList.toggle('on', i < lit));
}

// ---- race state ---------------------------------------------------------

function updateControls() {
  const idle = timerOnline && raceState === 'idle';
  btnStart.disabled = !idle;
  btnReset.disabled = !timerOnline;
  timerWarning.hidden = timerOnline;
  lapsInput.disabled = btnLapsMinus.disabled = btnLapsPlus.disabled = !idle;
  renderOrder();
  renderLapCounter();
}

function renderLapCounter() {
  if (raceState === 'running') {
    lapCounter.textContent = `LAP ${Math.min(leaderLaps + 1, raceLaps)} / ${raceLaps}`;
  } else if (raceState === 'finished') {
    lapCounter.textContent = `${raceLaps} LAPS · others finish on their next crossing`;
  } else if (raceState === 'countdown') {
    lapCounter.textContent = `${raceLaps} LAPS`;
  } else {
    lapCounter.textContent = '';
  }
}

function setState(data) {
  raceState = data.state;
  stateCard.classList.toggle('idle', raceState === 'idle');
  stateCard.classList.toggle('countdown', raceState === 'countdown');
  stateCard.classList.toggle('running', raceState === 'running');
  stateCard.classList.toggle('finished', raceState === 'finished');
  if (data.laps) raceLaps = data.laps;

  clearInterval(clockTimer);
  if (raceState === 'finished') {
    stateLabel.textContent = 'CHEQUERED FLAG';
    stateSub.textContent = `${data.winner || 'Winner'} wins · press RESET for a new race`;
    renderLights(0);
    startedAt = null;
    clockEl.textContent = formatElapsed(data.elapsed_s || 0);
  } else if (raceState === 'running') {
    stateLabel.textContent = 'RACE IN PROGRESS';
    stateSub.textContent = 'timing live';
    renderLights(0);
    startedAt = data.started_at || Date.now();
    clockTimer = setInterval(tickClock, 50);
    tickClock();
  } else if (raceState === 'countdown') {
    stateLabel.textContent = 'STARTING';
    stateSub.textContent = 'lights out and away we go';
    renderLights(data.lit || 0);
    startedAt = null;
    clockEl.textContent = formatElapsed(0);
  } else {
    stateLabel.textContent = 'READY';
    stateSub.textContent = 'press START here or on the button';
    renderLights(0);
    startedAt = null;
  }
  updateControls();
}

// ---- leaderboard --------------------------------------------------------

function cell(className, text) {
  const span = document.createElement('span');
  span.className = className;
  span.textContent = text;
  return span;
}

// Standings come pre-sorted from the server: classified racers by place,
// then the rest by laps completed.
function renderStandings(list) {
  boardList.innerHTML = '';
  leaderLaps = list.length ? Math.max(...list.map((r) => r.laps)) : 0;
  renderLapCounter();
  if (!list.length) {
    boardList.appendChild(emptyRow);
    return;
  }

  list.forEach((r, i) => {
    const li = document.createElement('li');
    li.className = `row ${r.place ? 'flag-row' : 'running-row'}`;
    if (r.place) li.dataset.place = r.place;

    const isUnknown = !r.racer || r.racer.startsWith('UNKNOWN');
    li.append(
      cell('pos', r.place || i + 1),
      cell(`racer ${isUnknown ? 'unknown' : ''}`, r.racer || 'Unknown'),
      cell('laps-col', `${r.laps}/${raceLaps}`),
      cell('last-lap', r.last_lap_s != null ? formatElapsed(r.last_lap_s) : '—'),
      cell('time', formatElapsed(r.elapsed_s || 0)),
    );
    boardList.appendChild(li);
  });
}

// ---- start lights order editor -------------------------------------------

function ledLabel(index) {
  const gpio = ledPins[index];
  const phys = PHYSICAL_PIN[gpio];
  return { title: `Light ${String.fromCharCode(65 + index)}`,
           detail: `GPIO${gpio}${phys ? ` · pin ${phys}` : ''}` };
}

function smallButton(text, title, onClick, disabled) {
  const b = document.createElement('button');
  b.className = 'btn btn-small';
  b.textContent = text;
  b.title = title;
  b.disabled = disabled;
  b.addEventListener('click', onClick);
  return b;
}

function move(pos, delta) {
  const to = pos + delta;
  if (to < 0 || to >= draftOrder.length) return;
  [draftOrder[pos], draftOrder[to]] = [draftOrder[to], draftOrder[pos]];
  renderOrder();
}

function renderOrder() {
  if (!draftOrder.length) return;
  const editable = timerOnline && raceState === 'idle';
  orderList.innerHTML = '';

  draftOrder.forEach((led, pos) => {
    const li = document.createElement('li');
    li.className = 'order-row';

    const step = document.createElement('span');
    step.className = 'order-step';
    step.textContent = `${pos + 1}.`;

    const { title, detail } = ledLabel(led);
    const label = document.createElement('span');
    label.className = 'order-led';
    label.textContent = title;
    const small = document.createElement('small');
    small.textContent = detail;
    label.appendChild(small);

    const buttons = document.createElement('span');
    buttons.className = 'order-buttons';
    buttons.append(
      smallButton('▲', 'Light earlier', () => move(pos, -1), !editable || pos === 0),
      smallButton('▼', 'Light later', () => move(pos, 1), !editable || pos === draftOrder.length - 1),
      smallButton('Test', 'Flash this light for 1 s', () => socket.emit('command', { cmd: 'test_led', led }), !editable),
    );

    li.append(step, label, buttons);
    orderList.appendChild(li);
  });

  const changed = draftOrder.join() !== savedOrder.join();
  btnOrderSave.disabled = !editable || !changed;
  btnOrderRevert.disabled = !changed;
}

btnOrderSave.addEventListener('click', () => {
  socket.emit('command', { cmd: 'set_led_order', order: draftOrder });
});

btnOrderRevert.addEventListener('click', () => {
  draftOrder = [...savedOrder];
  renderOrder();
});

// ---- laps setting -----------------------------------------------------------

function sendLaps(n) {
  const laps = Math.max(1, Math.min(99, Math.round(Number(n)) || 1));
  lapsInput.value = laps;
  socket.emit('command', { cmd: 'set_laps', laps });
}

btnLapsMinus.addEventListener('click', () => sendLaps(Number(lapsInput.value) - 1));
btnLapsPlus.addEventListener('click', () => sendLaps(Number(lapsInput.value) + 1));
lapsInput.addEventListener('change', () => sendLaps(lapsInput.value));

btnStart.addEventListener('click', () => socket.emit('command', { cmd: 'start' }));
btnReset.addEventListener('click', () => socket.emit('command', { cmd: 'reset' }));

// ---- socket events --------------------------------------------------------

socket.on('connect', () => {
  connDot.className = 'conn-dot online';
  connLabel.textContent = 'live';
});

socket.on('disconnect', () => {
  connDot.className = 'conn-dot offline';
  connLabel.textContent = 'disconnected';
  timerOnline = false;
  updateControls();
});

socket.on('timer', (online) => {
  timerOnline = online;
  updateControls();
});

socket.on('state', (data) => setState(data));

socket.on('config', (cfg) => {
  ledPins = cfg.led_pins || [];
  if (cfg.laps) {
    if (document.activeElement !== lapsInput) lapsInput.value = cfg.laps;
    if (raceState === 'idle') raceLaps = cfg.laps;
  }
  const unsaved = draftOrder.length && draftOrder.join() !== savedOrder.join();
  savedOrder = cfg.led_order || [];
  if (!unsaved || draftOrder.join() === savedOrder.join()) draftOrder = [...savedOrder];
  renderLights(raceState === 'countdown' ? lightsEl.querySelectorAll('.on').length : 0);
  renderOrder();
});

socket.on('standings', (list) => renderStandings(Array.isArray(list) ? list : []));

renderLights(0);
