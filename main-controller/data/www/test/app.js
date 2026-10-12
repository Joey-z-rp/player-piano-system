const KEYS = 88;
const NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
const noteName = (key) => NAMES[(key + 21) % 12] + (Math.floor((key + 21) / 12) - 1);
const isBlack = (key) => NAMES[(key + 21) % 12].includes("#");

const keyboard = document.getElementById("keyboard");
const keySelect = document.getElementById("key");
const dutyRange = document.getElementById("duty-range");
const dutyInput = document.getElementById("duty");
const holdInput = document.getElementById("hold");
const commandInput = document.getElementById("command");
const historyBox = document.getElementById("history");
const log = document.getElementById("log");

// ---- settings, remembered in this browser ----
const saved = (() => {
  try { return JSON.parse(localStorage.getItem("driver-test") || "{}"); } catch { return {}; }
})();
const settings = { key: 39, duty: 40, hold: 300, history: [], ...saved };
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

async function post(url, body, label) {
  try {
    const response = await fetch(url, body === undefined ? { method: "POST" } : {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
    });
    const reply = await response.json();
    write(response.ok ? label : `${label}: failed (${reply.error})`);
    return response.ok;
  } catch (error) {
    write(`${label}: no reply from the controller (${error.message})`);
    return false;
  }
}

const send = (command) => post("/api/driver/command", { command }, command);

function flash(key, ms) {
  const el = keyboard.querySelector(`[data-key="${key}"]`);
  if (!el) return;
  el.classList.add("playing");
  setTimeout(() => el.classList.remove("playing"), ms);
}

async function strike(key) {
  const hold = Number(holdInput.value) || 300;
  flash(key, hold);
  if (await send(`P:${key}:${formatDuty(Number(dutyInput.value))}`)) {
    setTimeout(() => send(`R:${key}:0`), hold);
  }
}

// ---- keyboard ----
function selectKey(key) {
  settings.key = key;
  keySelect.value = String(key);
  keyboard.querySelectorAll(".selected").forEach((el) => el.classList.remove("selected"));
  keyboard.querySelector(`[data-key="${key}"]`)?.classList.add("selected");
  save();
}

function keyButton(key) {
  const button = document.createElement("button");
  button.type = "button";
  button.dataset.key = key;
  button.className = isBlack(key) ? "black" : "white";
  button.setAttribute("aria-label", `Strike ${noteName(key)} (key ${key})`);
  button.title = `${noteName(key)} · key ${key}`;
  if (!isBlack(key) && noteName(key).startsWith("C")) button.textContent = noteName(key);
  button.addEventListener("click", (event) => {
    event.stopPropagation();
    selectKey(key);
    strike(key);
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
  keySelect.append(new Option(`${key} · ${noteName(key)}`, key));
}

// ---- key controls ----
keySelect.addEventListener("change", () => selectKey(Number(keySelect.value)));
dutyRange.addEventListener("input", () => { dutyInput.value = dutyRange.value; settings.duty = Number(dutyRange.value); save(); });
dutyInput.addEventListener("change", () => {
  const duty = Math.min(100, Math.max(0, Math.round(Number(dutyInput.value) * 4) / 4 || 0));
  dutyInput.value = dutyRange.value = duty;
  settings.duty = duty;
  save();
});
holdInput.addEventListener("change", () => { settings.hold = Number(holdInput.value); save(); });

document.getElementById("strike").addEventListener("click", () => strike(settings.key));
document.getElementById("press").addEventListener("click", () =>
  send(`P:${settings.key}:${formatDuty(Number(dutyInput.value))}`));
document.getElementById("release").addEventListener("click", () => send(`R:${settings.key}:0`));

// ---- pedal and everything ----
const pedal = (body, label) => post("/api/pedal", body, label);
document.getElementById("pedal-press").addEventListener("click", () => pedal({ down: true }, "pedal pressed"));
document.getElementById("pedal-release").addEventListener("click", () => pedal({ down: false }, "pedal released"));
document.getElementById("release-all").addEventListener("click", () =>
  post("/api/playback/stop", undefined, "all keys and the pedal released"));
document.getElementById("reset").addEventListener("click", () =>
  post("/api/driver/reset", undefined, "drivers restarting"));

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

document.getElementById("form").addEventListener("submit", async (event) => {
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

// ---- start ----
dutyInput.value = dutyRange.value = settings.duty;
holdInput.value = settings.hold;
selectKey(settings.key);
drawHistory();
keyboard.querySelector(`[data-key="${settings.key}"]`)?.scrollIntoView({ block: "nearest", inline: "center" });
