// The driver test page: keys (by velocity or duty, held or struck), the
// pedal, raw commands and the log. What the table does with a key and its
// offsets are in key-info.js; the listening tests in listen.js.
const KEYS = 88;
const NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
const noteName = (key) => NAMES[(key + 21) % 12] + (Math.floor((key + 21) / 12) - 1);
const isBlack = (key) => NAMES[(key + 21) % 12].includes("#");
const $ = (id) => document.getElementById(id);

const keyboard = $("keyboard");
const keySelect = $("key");
const velocityRange = $("velocity-range");
const velocityInput = $("velocity");
const pedalBox = $("pedal");
const dutyRange = $("duty-range");
const dutyInput = $("duty");
const holdInput = $("hold");
const commandInput = $("command");
const historyBox = $("history");
const log = $("log");

// ---- settings, remembered in this browser ----
const saved = (() => {
  try { return JSON.parse(localStorage.getItem("driver-test") || "{}"); } catch { return {}; }
})();
const settings = {
  key: 39, mode: "velocity", style: "press", velocity: 64, pedal: false, duty: 40, hold: 300, history: [],
  rampVelocities: "16, 32, 48, 64, 80, 96, 112, 127", rampGap: 800,
  rangeFrom: 34, rangeTo: 44, rangeGap: 500,
  repeatFirst: 64, repeatSecond: 64, repeatInterval: 150,
  ...saved,
};
const save = () => {
  try { localStorage.setItem("driver-test", JSON.stringify(settings)); } catch { /* not available */ }
};

// ---- talking to the controller ----
const formatDuty = (duty) => String(Math.round(duty * 100) / 100);

function write(line) {
  const time = new Date().toLocaleTimeString();
  log.textContent += `${time}  ${line}\n`;
  const lines = log.textContent.split("\n");
  if (lines.length > 300) log.textContent = lines.slice(-300).join("\n");
  log.scrollTop = log.scrollHeight;
}

// Returns the reply, or null if it failed (said in the log).
async function post(url, body, label) {
  try {
    const response = await fetch(url, body === undefined ? { method: "POST" } : {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
    });
    const reply = await response.json();
    if (!response.ok) {
      write(`${label}: failed (${reply.error})`);
      return null;
    }
    if (label) write(label);
    return reply;
  } catch (error) {
    write(`${label}: no reply from the controller (${error.message})`);
    return null;
  }
}

const send = (command) => post("/api/driver/command", { command }, command);

function flash(key, ms, delay = 0) {
  const el = keyboard.querySelector(`[data-key="${key}"]`);
  if (!el) return;
  setTimeout(() => {
    el.classList.add("playing");
    setTimeout(() => el.classList.remove("playing"), ms);
  }, delay);
}

const velocityMode = () => settings.mode === "velocity";
const pressStyle = () => settings.style === "press";
const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// ---- keys held while pressed ----
// With "Pedal down", the pedal is pressed before the first key and kept down
// until the box is cleared (a test, Stop or Release all lets it up).
let pedalHeld = false;
const held = new Map(); // key -> its press, still being sent

function pressKey(key) {
  if (held.has(key)) return;
  keyboard.querySelector(`[data-key="${key}"]`)?.classList.add("playing");
  held.set(key, (async () => {
    if (!velocityMode()) {
      await send(`P:${key}:${formatDuty(Number(dutyInput.value))}`);
      return;
    }
    if (settings.pedal && !pedalHeld) {
      pedalHeld = true;
      await post("/api/pedal", { down: true }, "pedal pressed");
      await wait(150); // dampers up before the key strikes
    }
    const reply = await post("/api/driver/note", { key, velocity: settings.velocity, pedal: settings.pedal },
      `${noteName(key)} down at velocity ${settings.velocity}`);
    if (reply) write(`    ${reply.queued}`);
  })());
}

async function releaseKey(key) {
  const pressing = held.get(key);
  if (!pressing) return;
  held.delete(key);
  keyboard.querySelector(`[data-key="${key}"]`)?.classList.remove("playing");
  await pressing; // so the release can't overtake the press
  send(`R:${key}:0`);
}

function letPedalUp() {
  pedalHeld = false;
}
const hold = () => Number(holdInput.value) || 300;

// Plays [[key, velocity, on ms, off ms], ...] the way a piece plays them.
async function playNotes(notes, label) {
  letPedalUp(); // a test ends with the pedal up
  const reply = await post("/api/test/play", { notes, pedal: settings.pedal },
    `${label}${settings.pedal ? ", pedal down" : ""}`);
  if (!reply) return null;
  if (reply.merged) write(`    ${reply.merged} merged: too soon to strike again`);
  for (const [ms, command] of reply.presses) write(`    ${String(ms).padStart(5)} ms  ${command}`);
  const lead = reply.presses.length ? reply.presses[0][0] : 0;
  notes.forEach(([key, , on, off]) => flash(key, off - on, on + lead));
  return reply;
}

async function strike(key) {
  if (velocityMode()) {
    playNotes([[key, settings.velocity, 0, hold()]], `${noteName(key)} at velocity ${settings.velocity}`);
    return;
  }
  flash(key, hold());
  if (await send(`P:${key}:${formatDuty(Number(dutyInput.value))}`)) {
    setTimeout(() => send(`R:${key}:0`), hold());
  }
}

// ---- keyboard ----
function selectKey(key) {
  settings.key = key;
  keySelect.value = String(key);
  keyboard.querySelectorAll(".selected").forEach((el) => el.classList.remove("selected"));
  keyboard.querySelector(`[data-key="${key}"]`)?.classList.add("selected");
  save();
  loadKeyInfo();
}

function keyButton(key) {
  const button = document.createElement("button");
  button.type = "button";
  button.dataset.key = key;
  button.className = isBlack(key) ? "black" : "white";
  button.setAttribute("aria-label", `Strike ${noteName(key)} (key ${key})`);
  button.title = `${noteName(key)} · key ${key}`;
  if (!isBlack(key) && noteName(key).startsWith("C")) button.textContent = noteName(key);
  button.addEventListener("keydown", (event) => {
    if ((event.key !== " " && event.key !== "Enter") || event.repeat) return;
    event.preventDefault();
    selectKey(key);
    pressStyle() ? pressKey(key) : strike(key);
  });
  button.addEventListener("keyup", (event) => {
    if (event.key === " " || event.key === "Enter") releaseKey(key);
  });
  return button;
}

// White keys sit in a row; each black key is placed over the boundary
// between the two white keys around it.
const WHITE_WIDTH = 22, BLACK_WIDTH = 14;
let whites = 0;
for (let key = 0; key < KEYS; key++) {
  const button = keyButton(key);
  if (isBlack(key)) {
    button.style.left = `${whites * WHITE_WIDTH - BLACK_WIDTH / 2}px`;
  } else {
    whites++;
  }
  keyboard.append(button);
  for (const select of [keySelect, $("range-from"), $("range-to")]) {
    select.append(new Option(`${key} · ${noteName(key)}`, key));
  }
}

// Mouse, pen or touch: held while pressed (sliding along the keys plays each
// one in turn), or struck for the hold time.
let pointerKey = null;
function keyAt(event) {
  const el = document.elementFromPoint(event.clientX, event.clientY)?.closest("[data-key]");
  return el && keyboard.contains(el) ? Number(el.dataset.key) : null;
}
keyboard.addEventListener("pointerdown", (event) => {
  const key = keyAt(event);
  if (key === null || event.button !== 0) return;
  event.preventDefault();
  selectKey(key);
  if (!pressStyle()) {
    strike(key);
    return;
  }
  pointerKey = key;
  pressKey(key);
});
window.addEventListener("pointermove", (event) => {
  if (pointerKey === null) return;
  const key = keyAt(event);
  if (key === pointerKey) return;
  releaseKey(pointerKey);
  pointerKey = key;
  if (key !== null) {
    selectKey(key);
    pressKey(key);
  }
});
function letGo() {
  if (pointerKey !== null) releaseKey(pointerKey);
  pointerKey = null;
}
window.addEventListener("pointerup", letGo);
window.addEventListener("pointercancel", letGo); // e.g. the keyboard scrolled instead
window.addEventListener("blur", () => { letGo(); [...held.keys()].forEach(releaseKey); });

// ---- key controls ----
function showMode() {
  document.querySelectorAll('input[name="mode"]').forEach((r) => (r.checked = r.value === settings.mode));
  $("velocity-row").hidden = !velocityMode();
  $("duty-row").hidden = velocityMode();
  document.querySelectorAll('input[name="style"]').forEach((r) => (r.checked = r.value === settings.style));
  const how = pressStyle() ? "Press a key to play it, and let go to release it" : "Click a key to strike it for the hold time";
  $("mode-hint").textContent = velocityMode()
    ? `${how}, at this velocity, the way a piece plays it (from the velocity table).`
    : `${how}, with this duty: a plain press, whatever the table says.`;
  drawKeyInfo();
}
document.querySelectorAll('input[name="style"]').forEach((radio) => radio.addEventListener("change", () => {
  settings.style = radio.value;
  save();
  showMode();
}));
document.querySelectorAll('input[name="mode"]').forEach((radio) => radio.addEventListener("change", () => {
  settings.mode = radio.value;
  save();
  showMode();
  loadKeyInfo();
}));

keySelect.addEventListener("change", () => selectKey(Number(keySelect.value)));
function setVelocity(value) {
  const velocity = Math.min(127, Math.max(1, Math.round(Number(value)) || 64));
  velocityInput.value = velocityRange.value = settings.velocity = velocity;
  save();
  loadKeyInfo();
}
velocityRange.addEventListener("input", () => setVelocity(velocityRange.value));
velocityInput.addEventListener("change", () => setVelocity(velocityInput.value));
pedalBox.addEventListener("change", () => {
  settings.pedal = pedalBox.checked;
  save();
  loadKeyInfo();
  if (!settings.pedal && pedalHeld) {
    letPedalUp();
    post("/api/pedal", { down: false }, "pedal released");
  }
});
dutyRange.addEventListener("input", () => { dutyInput.value = dutyRange.value; settings.duty = Number(dutyRange.value); save(); });
dutyInput.addEventListener("change", () => {
  const duty = Math.min(100, Math.max(0, Math.round(Number(dutyInput.value) * 4) / 4 || 0));
  dutyInput.value = dutyRange.value = duty;
  settings.duty = duty;
  save();
});
holdInput.addEventListener("change", () => { settings.hold = Number(holdInput.value); save(); });

$("strike").addEventListener("click", () => strike(settings.key));
$("press").addEventListener("click", () => {
  if (!velocityMode()) {
    send(`P:${settings.key}:${formatDuty(Number(dutyInput.value))}`);
  } else if (keyInfo) {
    send(keyInfo.command); // held until Release (the pedal isn't pressed for it)
  }
});
$("release").addEventListener("click", () => send(`R:${settings.key}:0`));

// ---- pedal and everything ----
const pedal = (body, label) => post("/api/pedal", body, label);
$("pedal-press").addEventListener("click", () => { pedalHeld = true; pedal({ down: true }, "pedal pressed"); });
$("pedal-release").addEventListener("click", () => { letPedalUp(); pedal({ down: false }, "pedal released"); });
$("release-all").addEventListener("click", () => {
  letPedalUp();
  post("/api/playback/stop", undefined, "all keys and the pedal released");
});
$("reset").addEventListener("click", () => post("/api/driver/reset", undefined, "drivers restarting"));

// ---- typed commands, with recent ones to click again ----
function drawHistory() {
  historyBox.replaceChildren(...settings.history.map((command) => {
    const button = document.createElement("button");
    button.type = "button";
    button.textContent = command;
    button.title = "Send again";
    button.addEventListener("click", () => send(command));
    return button;
  }));
}

$("form").addEventListener("submit", async (event) => {
  event.preventDefault();
  const command = commandInput.value.trim();
  if (!command) return;
  if (await send(command)) {
    settings.history = [command, ...settings.history.filter((c) => c !== command)].slice(0, 10);
    save();
    drawHistory();
    commandInput.value = "";
  }
  commandInput.focus();
});

// ---- start, once key-info.js and listen.js have loaded too ----
document.addEventListener("DOMContentLoaded", () => {
  dutyInput.value = dutyRange.value = settings.duty;
  velocityInput.value = velocityRange.value = settings.velocity;
  pedalBox.checked = settings.pedal;
  holdInput.value = settings.hold;
  showMode();
  selectKey(settings.key);
  loadOffsets();
  drawHistory();
  keyboard.querySelector(`[data-key="${settings.key}"]`)?.scrollIntoView({ block: "nearest", inline: "center" });
});
