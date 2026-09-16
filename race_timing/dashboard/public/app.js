const socket = io();

const connDot = document.getElementById('conn-dot');
const connLabel = document.getElementById('conn-label');
const stateCard = document.getElementById('state-card');
const stateLabel = document.getElementById('state-label');
const stateSub = document.getElementById('state-sub');
const clockEl = document.getElementById('clock');
const boardList = document.getElementById('board-list');
const emptyRow = document.getElementById('empty-row');

let startedAt = null;   // epoch ms from the race timer, or null when idle
let clockTimer = null;

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

function setRunning(running, startedAtMs) {
  stateCard.classList.toggle('running', running);
  stateCard.classList.toggle('idle', !running);
  stateLabel.textContent = running ? 'RACE IN PROGRESS' : 'READY';
  stateSub.textContent = running ? 'timing live' : 'waiting for start signal';

  clearInterval(clockTimer);
  if (running) {
    startedAt = startedAtMs || Date.now();
    clockTimer = setInterval(tickClock, 50);
    tickClock();
  } else {
    startedAt = null;
  }
}

function clearBoard() {
  boardList.innerHTML = '';
  boardList.appendChild(emptyRow);
}

function addFinisher({ place, racer, elapsed_s }) {
  emptyRow.remove();

  const li = document.createElement('li');
  li.className = 'row';
  li.dataset.place = place;

  const isUnknown = !racer || racer.startsWith('UNKNOWN');

  li.innerHTML = `
    <span class="pos">${place}</span>
    <span class="racer ${isUnknown ? 'unknown' : ''}">${racer || 'Unknown'}</span>
    <span class="time">${formatElapsed(elapsed_s)}</span>
  `;
  boardList.appendChild(li);
  boardList.scrollTop = boardList.scrollHeight;
}

socket.on('connect', () => {
  connDot.className = 'conn-dot online';
  connLabel.textContent = 'live';
});

socket.on('disconnect', () => {
  connDot.className = 'conn-dot offline';
  connLabel.textContent = 'disconnected';
});

socket.on('state', (data) => {
  if (data.state === 'running') {
    clearBoard();
    setRunning(true, data.started_at);
  } else {
    setRunning(false);
  }
});

socket.on('finish', (data) => addFinisher(data));

socket.on('snapshot', (list) => {
  if (!Array.isArray(list) || list.length === 0) return;
  clearBoard();
  list.forEach(addFinisher);
});
