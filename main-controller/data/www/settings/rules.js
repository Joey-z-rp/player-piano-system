// The playing rules (the play config, solenoid heat included): each setting
// shows its default, and is marked when it differs from it. Edits wait in a
// save bar until saved (the piano checks them, and says what's wrong) or
// dropped. Uses ui (/assets/app.js).
(() => {
const limitsBody = document.getElementById("limits");
const limitsTable = document.getElementById("limits-table");
const savebar = document.getElementById("savebar");
let defaults = null;
let saved = null; // as the piano has it

const settingRows = () => [...document.querySelectorAll(".setting[data-key]")];
const show = (value, kind) => (kind === "list" ? value.join(", ")
  : typeof value === "boolean" ? (value ? "duty squared" : "duty") : String(value));
const field = (row) => row.querySelector("input, select");
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);

function limitRow(limit = ["", "", ""]) {
  const tr = ui.el("tr");
  for (const value of limit) {
    const input = ui.el("input", { class: "input input-sm", type: "number", min: 0, max: 2000, value });
    input.addEventListener("input", changed);
    tr.append(ui.el("td", {}, input));
  }
  const remove = ui.el("button", { class: "btn btn-ghost btn-icon btn-sm", type: "button", "aria-label": "Remove this row",
    title: "Remove this row", html: ui.icon("close") });
  remove.addEventListener("click", () => {
    tr.remove();
    changed();
  });
  tr.append(ui.el("td", {}, remove));
  return tr;
}

function readLimits() {
  return [...limitsBody.querySelectorAll("tr")].map((tr) =>
    [...tr.querySelectorAll("input")].map((input) => Number(input.value)));
}

function read() {
  const out = { repeat_limits: readLimits() };
  for (const row of settingRows()) {
    const value = field(row).value.trim();
    const kind = row.dataset.kind;
    out[row.dataset.key] = kind === "text" ? value
      : kind === "list" ? value.split(/[\s,]+/).filter(Boolean).map(Number)
      : kind === "select" ? value === "true"
      : Number(value);
  }
  return out;
}

// Marks what differs from the defaults, and shows the save bar while
// anything differs from what's saved.
function changed() {
  if (!defaults) return;
  const now = read();
  for (const row of settingRows()) {
    row.classList.toggle("changed", !same(now[row.dataset.key], defaults[row.dataset.key]));
  }
  limitsTable.classList.toggle("changed", !same(now.repeat_limits, defaults.repeat_limits));
  const edits = Object.keys(now).filter((key) => !same(now[key], saved[key])).length;
  savebar.hidden = edits === 0;
  document.getElementById("savebar-text").textContent = `${edits} unsaved ${edits === 1 ? "change" : "changes"}`;
}

function fill(config) {
  saved = config;
  for (const row of settingRows()) {
    const key = row.dataset.key;
    field(row).value = row.dataset.kind === "select" ? String(config[key]) : show(config[key], row.dataset.kind);
    row.querySelector(".default").textContent = `Default ${show(defaults[key], row.dataset.kind)}`;
    field(row).oninput = field(row).onchange = changed;
  }
  limitsBody.replaceChildren(...config.repeat_limits.map(limitRow));
  const measured = (kind) => ["ohms", "rth", "tau_s"].every((n) => config[`${kind}_coil_${n}`] > 0);
  const watched = [measured("key") && "keys", measured("pedal") && "pedal"].filter(Boolean);
  const chip = document.getElementById("heat-watched");
  chip.textContent = watched.length ? `Watching: ${watched.join(" and ")}` : "Not watched yet";
  chip.className = "chip" + (watched.length ? " accent" : "");
  document.getElementById("limits-default").textContent =
    "Default: " + defaults.repeat_limits.map(([from, soft, loud]) => `from ${from}: ${soft} / ${loud} ms`).join(", ");
  changed();
}

async function loadRules() {
  try {
    const body = await ui.api("/api/config");
    defaults = body.defaults;
    fill(body.config);
  } catch (error) {
    ui.toast(`Couldn't load the playing rules: ${error.message}`, { error: true });
  }
}

document.getElementById("add-limit").addEventListener("click", () => {
  limitsBody.append(limitRow());
  changed();
});

document.getElementById("save").addEventListener("click", async () => {
  const body = await ui.request("/api/config", { json: read() });
  if (body) {
    fill(body.config);
    ui.toast("Saved: the new rules apply from the next Play");
  }
});

document.getElementById("discard").addEventListener("click", () => fill(saved));

document.getElementById("restore").addEventListener("click", async () => {
  const sure = await ui.confirm({ title: "Restore the default rules?", message: "Every playing rule and the solenoid heat " +
    "settings go back to their defaults.", confirm: "Restore defaults", danger: true });
  if (!sure) return;
  const body = await ui.request("/api/config/reset", { method: "POST" });
  if (body) {
    fill(body.config);
    ui.toast("Back to the defaults: they apply from the next Play");
  }
});

// Leaving with unsaved rules: the browser asks first.
window.addEventListener("beforeunload", (event) => {
  if (!savebar.hidden) event.preventDefault();
});

// The solenoids' heat now.
ui.onStatus((status) => {
  const heat = status.heat;
  document.getElementById("heat-now").textContent = heat && heat.watched ? ui.heatText(heat).line : "";
});

loadRules();
})();
