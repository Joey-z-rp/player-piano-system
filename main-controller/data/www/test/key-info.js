// What the velocity table does with the selected key (its duty curve, landing,
// clamp and the command it sends), and its offsets editor. Uses the helpers
// and settings in app.js.
// ---- what the table does with the key ----
let keyInfo = null;
let infoRequest = 0;
async function loadKeyInfo(rebuildOffsets = false) {
  const request = ++infoRequest;
  let info = null;
  try {
    const response = await fetch(`/api/driver/key?key=${settings.key}&velocity=${settings.velocity}` +
      `&pedal=${settings.pedal ? 1 : 0}`);
    info = response.ok ? await response.json() : null;
  } catch { /* shown as unavailable */ }
  if (request !== infoRequest) return; // a newer one is on its way
  keyInfo = info;
  drawKeyInfo(rebuildOffsets);
}

const SVG = "http://www.w3.org/2000/svg";
function svgEl(name, attrs) {
  const el = document.createElementNS(SVG, name);
  Object.entries(attrs).forEach(([k, v]) => el.setAttribute(k, v));
  return el;
}

// ---- per-key offsets ----
// As the controller does it (key_offsets.cpp OffsetCurve::adjust): straight
// lines between the points, flat beyond them, rounded half away from zero.
function divideRounded(n, d) {
  const half = Math.floor(d / 2);
  return n >= 0 ? Math.floor((n + half) / d) : -Math.floor((-n + half) / d);
}
function offsetAt(points, v) {
  if (!points.length) return 0;
  if (v <= points[0][0]) return points[0][1];
  const last = points[points.length - 1];
  if (v >= last[0]) return last[1];
  let i = 1;
  while (points[i][0] < v) i++;
  const [v0, o0] = points[i - 1], [v1, o1] = points[i], span = v1 - v0;
  return divideRounded(o0 * span + (o1 - o0) * (v - v0), span);
}
const adjust = (points, v) => Math.min(127, Math.max(1, v + offsetAt(points, v)));

// What's wrong with a curve, as the controller would say it, or "".
const MAX_POINTS = 8; // MAX_OFFSET_POINTS in key_offsets.h
function curveProblem(points) {
  if (points.length > MAX_POINTS) return `a curve can have up to ${MAX_POINTS} points`;
  if (points.some(([v, o]) => !Number.isInteger(v) || v < 1 || v > 127 || !Number.isInteger(o) || o < -60 || o > 60)) {
    return "each point needs a velocity from 1 to 127 and an offset from -60 to 60";
  }
  if (points.some((p, i) => i > 0 && p[0] <= points[i - 1][0])) return "the velocities must rise from point to point";
  for (let v = 2; v <= 127; v++) {
    if (adjust(points, v) < adjust(points, v - 1)) {
      return `velocity ${v} would play softer than ${v - 1}: an offset can't fall by more than 1 per velocity`;
    }
  }
  return "";
}

// Where the offsets run out of room: velocities pushed past 1 or 127.
function noRoom(points) {
  const over = [], under = [];
  for (let v = 1; v <= 127; v++) {
    const want = v + offsetAt(points, v);
    if (want > 127) over.push(v);
    if (want < 1) under.push(v);
  }
  const span = (list) => (list.length > 1 ? `${list[0]}-${list[list.length - 1]}` : list[0]);
  return [over.length ? `velocities ${span(over)} already play at 127: no louder` : "",
    under.length ? `velocities ${span(under)} already play at 1: no softer` : ""].filter(Boolean).join("; ");
}

let allOffsets = {}; // key -> {up, pedal}, from /api/offsets
let draft = null; // the points being edited, for the curve shown
let draftFor = ""; // which key and curve the draft is: "key/up" or "key/pedal"

async function loadOffsets() {
  try {
    const response = await fetch("/api/offsets");
    if (response.ok) allOffsets = (await response.json()).keys || {};
  } catch { /* keep what we had */ }
  showAdjustedKeys();
}

function showAdjustedKeys() {
  keyboard.querySelectorAll(".adjusted").forEach((el) => el.classList.remove("adjusted"));
  const keys = Object.keys(allOffsets).map(Number).sort((a, b) => a - b);
  keys.forEach((key) => keyboard.querySelector(`[data-key="${key}"]`)?.classList.add("adjusted"));
  const box = $("adjusted-keys");
  if (!keys.length) {
    box.textContent = "No key has offsets.";
    return;
  }
  box.replaceChildren("Keys with offsets: ", ...keys.flatMap((key, i) => {
    const button = document.createElement("button");
    button.type = "button";
    button.textContent = noteName(key) + (allOffsets[key].pedal ? " (own pedal-down)" : "");
    button.addEventListener("click", () => selectKey(key));
    return i ? [" ", button] : [button];
  }), " ");
  const clear = document.createElement("button");
  clear.type = "button";
  clear.textContent = "Clear all";
  clear.addEventListener("click", async () => {
    if (!confirm("Remove the offsets from every key?")) return;
    if (await post("/api/offsets/clear", undefined, "every key's offsets cleared")) {
      draft = null;
      await loadOffsets();
      loadKeyInfo();
    }
  });
  box.append(clear);
}

// Which curve the editor works on: the pedal-down one with "Pedal down"
// ticked and a key with its own pedal-down entry, else the pedal-up one.
const editingPedal = () => settings.pedal && keyInfo && keyInfo.hasPedal;
const savedCurve = (info) => (editingPedal() ? info.offsets.pedal : info.offsets.up);

function pointRow(point) {
  const tr = document.createElement("tr");
  point.forEach((value, i) => {
    const td = document.createElement("td");
    const input = document.createElement("input");
    Object.assign(input, { type: "number", value, step: 1, ...(i ? { min: -60, max: 60 } : { min: 1, max: 127 }) });
    input.setAttribute("aria-label", i ? "offset" : "from velocity");
    input.addEventListener("input", () => { readDraft(); drawKeyInfo(false); });
    td.append(input);
    tr.append(td);
  });
  const remove = document.createElement("button");
  remove.type = "button";
  remove.textContent = "×";
  remove.title = remove.ariaLabel = "Remove this point";
  remove.addEventListener("click", () => { tr.remove(); readDraft(); drawKeyInfo(false); });
  const td = document.createElement("td");
  td.append(remove);
  tr.append(td);
  return tr;
}

function readDraft() {
  draft = [...$("points").querySelectorAll("tr")].map((tr) =>
    [...tr.querySelectorAll("input")].map((input) => Number(input.value)));
}

// The editor, for the key shown; rows rebuilt only when asked (not while typing).
function drawOffsets(rebuild) {
  const box = $("offsets");
  box.hidden = !velocityMode() || !keyInfo;
  if (box.hidden) return;
  const info = keyInfo;
  const pedalDown = editingPedal();
  const own = pedalDown && info.offsets.pedal !== null;
  const which = `${info.key}/${pedalDown ? "pedal" : "up"}`;
  if (rebuild || draftFor !== which) {
    draftFor = which;
    draft = pedalDown && !own ? null : (savedCurve(info) || []).map((p) => p.slice());
    $("points").replaceChildren(...(draft || []).map(pointRow));
  }
  $("offsets-title").textContent = `Offsets for ${noteName(info.key)}${pedalDown ? ", pedal down" : settings.pedal ? "" : ", pedal up"}`;
  $("own-pedal-row").hidden = !pedalDown;
  $("own-pedal").checked = own || draft !== null && pedalDown;
  $("offsets-edit").hidden = pedalDown && draft === null;
  $("offsets-hint").textContent = pedalDown && draft === null
    ? "With the pedal down it uses its pedal-up offsets."
    : settings.pedal && !info.hasPedal
      ? "It has no pedal-down entry, so these apply with the pedal up and down."
      : "From each point's velocity on, the key plays as if given that many velocities more (or fewer), " +
        "on straight lines between the points.";
  const problem = draft ? curveProblem(draft) : "";
  const message = $("offsets-message");
  const changed = draftFor === which && JSON.stringify(draft) !== JSON.stringify(savedCurve(info) ?? null);
  message.className = problem ? "error" : "";
  message.textContent = problem || (changed ? "not saved yet: the curve shows it" : "");
  $("save-offsets").disabled = Boolean(problem) || !changed;
  $("add-point").disabled = (draft || []).length >= MAX_POINTS;
  $("clear-offsets").textContent = pedalDown ? "Use the pedal-up offsets" : "No offsets for this key";
}

$("add-point").addEventListener("click", () => {
  readDraft();
  const last = draft[draft.length - 1];
  const point = last ? [Math.min(127, last[0] + 16), last[1]] : [settings.velocity, 0];
  $("points").append(pointRow(point));
  readDraft();
  drawKeyInfo(false);
});

$("own-pedal").addEventListener("change", () => {
  if ($("own-pedal").checked) {
    draft = (keyInfo.offsets.up || []).map((p) => p.slice()); // starting from the pedal-up ones
  } else {
    draft = null;
  }
  $("points").replaceChildren(...(draft || []).map(pointRow));
  drawKeyInfo(false);
});

async function saveOffsets(points) {
  const info = keyInfo;
  const body = { key: info.key, up: info.offsets.up, pedal: info.offsets.pedal };
  if (editingPedal()) body.pedal = points;
  else body.up = points || [];
  const reply = await post("/api/offsets/key", body,
    `${noteName(info.key)} offsets${editingPedal() ? " (pedal down)" : ""}: ` +
    (points === null ? "same as pedal up" : points.length ? points.map(([v, o]) => `${v}: ${o > 0 ? "+" : ""}${o}`).join(", ") : "none"));
  if (reply) {
    allOffsets = reply.keys || {};
    showAdjustedKeys();
    await loadKeyInfo(true);
  }
}
$("save-offsets").addEventListener("click", () => saveOffsets(draft));
$("clear-offsets").addEventListener("click", () => saveOffsets(editingPedal() ? null : []));

// Its duty at each velocity, pedal up and (if it has its own) down.
function drawCurve(info) {
  const W = 460, H = 250, m = { left: 38, right: 8, top: 8, bottom: 22 };
  // The layer it plays from now: the table's curve, and (with offsets, or a
  // draft of them) what it plays.
  const pedalLayer = info.pedal && info.tablePedalDuties;
  const table = pedalLayer ? info.tablePedalDuties : info.tableDuties;
  const points = draft ?? (pedalLayer || info.pedal ? info.offsets.pedal ?? info.offsets.up : info.offsets.up) ?? [];
  const played = table.map((_, i) => table[adjust(points, i + 1) - 1]);
  // Zoomed to the key's own duties (to the 10% around them), so an offset shows.
  const low = Math.max(0, Math.floor(Math.min(...table, ...played) / 10) * 10);
  const high = Math.min(100, Math.max(low + 10, Math.ceil(Math.max(...table, ...played) / 10) * 10));
  const x = (v) => m.left + ((v - 1) / 126) * (W - m.left - m.right);
  const y = (duty) => m.top + (1 - (duty - low) / (high - low)) * (H - m.top - m.bottom);
  const svg = svgEl("svg", { viewBox: `0 0 ${W} ${H}`, width: "100%", role: "img",
    "aria-label": `Duty by velocity for key ${info.key}: ${low}% to ${high}%` });
  svg.style.maxWidth = W + "px";
  for (let d = low; d <= high; d += 10) {
    svg.append(svgEl("line", { x1: m.left, x2: W - m.right, y1: y(d), y2: y(d), stroke: "#e1e0d9" }));
    const t = svgEl("text", { x: m.left - 5, y: y(d) + 3, class: "tick", "text-anchor": "end" });
    t.textContent = d + "%";
    svg.append(t);
  }
  [1, 32, 64, 96, 127].forEach((v) => {
    svg.append(svgEl("line", { x1: x(v), x2: x(v), y1: m.top, y2: H - m.bottom, stroke: "#f0efea" }));
    const t = svgEl("text", { x: x(v), y: H - 6, class: "tick", "text-anchor": v === 1 ? "start" : v === 127 ? "end" : "middle" });
    t.textContent = v;
    svg.append(t);
  });
  const line = (duties, colour, dash, width = 2) => svg.append(svgEl("polyline", {
    points: duties.map((d, i) => `${x(i + 1)},${y(d)}`).join(" "),
    fill: "none", stroke: colour, "stroke-width": width, ...(dash ? { "stroke-dasharray": "4 3" } : {}),
  }));
  if (points.length) line(table, "#8a8984", true, 1.5);
  line(played, "#2a78d6");
  const v = info.velocity;
  svg.append(svgEl("circle", { cx: x(v), cy: y(played[v - 1]), r: 4, fill: "#f2b33d", stroke: "#fff", "stroke-width": 1.5 }));
  return svg;
}

function drawKeyInfo(rebuildOffsets = false) {
  drawOffsets(rebuildOffsets);
  const box = $("key-info");
  box.hidden = !velocityMode();
  if (!velocityMode()) return;
  const facts = $("facts");
  if (!keyInfo) {
    $("curve").replaceChildren();
    facts.textContent = "The controller didn't say how it plays this key.";
    return;
  }
  const info = keyInfo;
  $("curve").replaceChildren(drawCurve(info));
  const lines = [];
  lines.push(`<b>${noteName(info.key)}</b> (key ${info.key}): ` +
    (info.calibrated ? "calibrated" : `<span class="warn">not calibrated: a straight default mapping</span>`) +
    (info.hasPedal ? ", with its own pedal-down entry" : info.pedal ? ", plays the same with the pedal down" : ""));
  lines.push(`At velocity ${info.velocity}${info.pedal && info.hasPedal ? " (pedal down)" : ""}` +
    (info.playedVelocity !== info.velocity ? `, played as ${info.playedVelocity} (its offset)` : "") +
    `: duty ${info.duty}%` + (info.landingMs ? `, hammer lands ${info.landingMs} ms after the press` : ""));
  lines.push(info.clamp
    ? `Clamp: ${info.clamp.duty}% for ${info.clamp.ms} ms from ${info.clamp.offsetMs} ms after it lands; then holds at ${info.hold}%`
    : `Coasts at ${info.coast}%, then holds at ${info.hold}%`);
  const range = info.pedal && info.pedalDuties ? info.pedalDuties : info.duties;
  const points = draft ?? (info.pedal && info.hasPedal ? info.offsets.pedal ?? info.offsets.up : info.offsets.up) ?? [];
  lines.push(`Duty from ${range[0]}% (velocity 1) to ${range[126]}% (127)` +
    (points.length ? `; <span style="color:#2a78d6">as played</span>, <span style="color:#8a8984">the table</span>` : ""));
  const room = noRoom(points);
  if (room) lines.push(`<span class="room">${room}</span>`);
  lines.push(`Sends <code>${info.command}</code>`);
  facts.innerHTML = lines.join("<br>");
}
