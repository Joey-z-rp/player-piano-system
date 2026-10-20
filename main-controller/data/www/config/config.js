// The play config: each field shows its saved value and its default, and is
// outlined when it differs from the default. Save sends them all; the
// controller checks them and says what's wrong.
const message = document.getElementById("message");
const limitsBody = document.getElementById("limits");
let defaults = null;

function say(text, error = false) {
  message.textContent = text;
  message.className = error ? "error" : "";
}

const rows = () => [...document.querySelectorAll(".row[data-key]")];
const show = (value, kind) => (kind === "list" ? value.join(", ") : String(value));
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);

function limitRow(limit = ["", "", ""]) {
  const tr = document.createElement("tr");
  for (const value of limit) {
    const td = document.createElement("td");
    const input = document.createElement("input");
    Object.assign(input, { type: "number", min: 0, max: 2000, value });
    input.addEventListener("input", markChanged);
    td.append(input);
    tr.append(td);
  }
  const remove = document.createElement("button");
  remove.type = "button";
  remove.textContent = "Remove";
  remove.addEventListener("click", () => { tr.remove(); markChanged(); });
  const td = document.createElement("td");
  td.append(remove);
  tr.append(td);
  return tr;
}

function readLimits() {
  return [...limitsBody.querySelectorAll("tr")].map((tr) =>
    [...tr.querySelectorAll("input")].map((input) => Number(input.value)));
}

function read() {
  const out = { repeat_limits: readLimits() };
  for (const row of rows()) {
    const value = row.querySelector("input").value.trim();
    const kind = row.dataset.kind;
    out[row.dataset.key] = kind === "text" ? value
      : kind === "list" ? value.split(/[\s,]+/).filter(Boolean).map(Number)
      : Number(value);
  }
  return out;
}

function markChanged() {
  if (!defaults) return;
  const now = read();
  for (const row of rows()) {
    row.classList.toggle("changed", !same(now[row.dataset.key], defaults[row.dataset.key]));
  }
  limitsBody.closest("table").classList.toggle("changed", !same(now.repeat_limits, defaults.repeat_limits));
}

function fill(config) {
  for (const row of rows()) {
    const key = row.dataset.key;
    row.querySelector("input").value = show(config[key], row.dataset.kind);
    row.querySelector(".default").textContent = `(default ${show(defaults[key], row.dataset.kind)})`;
    row.querySelector("input").oninput = markChanged;
  }
  limitsBody.replaceChildren(...config.repeat_limits.map(limitRow));
  document.getElementById("limits-default").textContent =
    "Default: " + defaults.repeat_limits.map(([from, soft, loud]) => `from ${from}: ${soft} / ${loud} ms`).join(", ") + ".";
  markChanged();
}

async function call(url, options) {
  try {
    const response = await fetch(url, options);
    const body = await response.json();
    if (!response.ok) {
      say(body.error || "the controller refused", true);
      return null;
    }
    return body;
  } catch (error) {
    say("request failed: " + error.message, true);
    return null;
  }
}

async function load() {
  const body = await call("/api/config");
  if (!body) return;
  defaults = body.defaults;
  fill(body.config);
}

document.getElementById("add-limit").addEventListener("click", () => {
  limitsBody.append(limitRow());
  markChanged();
});

document.getElementById("save").addEventListener("click", async () => {
  const body = await call("/api/config", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(read()),
  });
  if (body) {
    fill(body.config);
    say("Saved: applies from the next Play.");
  }
});

document.getElementById("restore").addEventListener("click", async () => {
  if (!confirm("Put every setting back to its default?")) return;
  const body = await call("/api/config/reset", { method: "POST" });
  if (body) {
    fill(body.config);
    say("Back to the defaults: applies from the next Play.");
  }
});

load();
