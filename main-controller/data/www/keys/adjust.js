// A key's adjustments, made by ear: velocity offsets (it plays as if given
// another velocity) and duty changes (a duty added to what the table gives,
// for what an offset can't reach), each with the pedal up and, if the key
// wants, its own with the pedal down. Worked out as the controller does them
// (key_offsets.cpp). Uses the helpers and settings in app.js, keyInfo and
// loadKeyInfo/drawKeyInfo from key-info.js, and ui (/assets/app.js).
const MAX_POINTS = 8; // MAX_OFFSET_POINTS in key_offsets.h

function divideRounded(n, d) {
  const half = Math.floor(d / 2);
  return n >= 0 ? Math.floor((n + half) / d) : -Math.floor((-n + half) / d);
}
// Straight lines between the points, flat beyond them.
function between(points, v, at) {
  if (!points.length) return 0;
  if (v <= points[0][0]) return points[0][1];
  const last = points[points.length - 1];
  if (v >= last[0]) return last[1];
  let i = 1;
  while (points[i][0] < v) i++;
  return at(points[i - 1], points[i], v);
}
// OffsetCurve::adjust: whole velocities, rounded half away from zero.
const offsetAt = (points, v) => between(points, v, ([v0, o0], [v1, o1]) =>
  divideRounded(o0 * (v1 - v0) + (o1 - o0) * (v - v0), v1 - v0));
const adjust = (points, v) => Math.min(127, Math.max(1, v + offsetAt(points, v)));
// DutyCurve::change: percent, rounded to 0.25% half away from zero.
const dutyChangeAt = (points, v) => between(points, v, ([v0, c0], [v1, c1]) => {
  const a = Math.round(c0 * 100), b = Math.round(c1 * 100), span = v1 - v0;
  return divideRounded(a * span + (b - a) * (v - v0), span * 25) / 4;
});

// The two kinds: where /api/driver/key gives their curves (and /api/offsets
// keeps them), their limits and how they're explained.
const KINDS = {
  velocity: {
    up: "up", pedal: "pedal", apiPedal: "pedal", label: "Offset", min: -60, max: 60, step: 1, name: "offsets",
    format: (o) => `${o > 0 ? "+" : ""}${o}`,
    hint: "From each point's velocity on, the key plays as if given that many velocities more (or fewer), " +
      "on straight lines between the points.",
  },
  duty: {
    up: "duty", pedal: "dutyPedal", apiPedal: "duty_pedal", label: "Duty change (%)", min: -50, max: 50, step: 0.25,
    name: "duty changes", format: (c) => `${c > 0 ? "+" : ""}${c}%`,
    hint: "Adds to (or takes from) the duty the table gives, on straight lines between the points: for what an " +
      "offset can't reach, like a key whose calibration stopped short of its loudest. Its landing time follows the duty.",
  },
};
const editKind = () => (settings.adjustKind in KINDS ? settings.adjustKind : "velocity");

let allOffsets = {}; // key -> {up, pedal, duty, duty_pedal}, from /api/offsets
let draft = null; // the points being edited (null: the pedal-up ones, with the pedal down)
let draftFor = ""; // which key, kind and curve the draft is: e.g. "41/duty/up"

// Which curve the editor works on: the pedal-down one with "Pedal down"
// ticked and a key with its own pedal-down entry, else the pedal-up one.
const editingPedal = () => settings.pedal && keyInfo && keyInfo.hasPedal;
const draftKey = (info) => `${info.key}/${editKind()}/${editingPedal() ? "pedal" : "up"}`;
const savedCurve = (info, kind = editKind()) => (editingPedal() ? info.offsets[KINDS[kind].pedal] : info.offsets[KINDS[kind].up]);

// The points each kind plays with now: the draft for the one being edited,
// else what's saved (with the pedal down, its own pedal-down ones if any).
function pointsShown(info, kind) {
  const k = KINDS[kind];
  if (kind === editKind() && draftFor === draftKey(info)) return draft ?? info.offsets[k.up] ?? [];
  return (info.pedal ? info.offsets[k.pedal] ?? info.offsets[k.up] : info.offsets[k.up]) ?? [];
}
// The table's duties for the layer it plays from now, and what it plays.
const tableShown = (info) => (info.pedal && info.tablePedalDuties ? info.tablePedalDuties : info.tableDuties);
function playedDuties(info) {
  const table = tableShown(info), offsets = pointsShown(info, "velocity"), duty = pointsShown(info, "duty");
  return table.map((_, i) => {
    const played = table[adjust(offsets, i + 1) - 1] + dutyChangeAt(duty, i + 1);
    return Math.round(Math.min(100, Math.max(0, played)) * 100) / 100;
  });
}

const span = (list) => (list.length > 1 ? `${list[0]}-${list[list.length - 1]}` : list[0]);

// What's wrong with a curve, as the controller would say it, or "".
function curveProblem(points, kind) {
  const k = KINDS[kind];
  if (points.length > MAX_POINTS) return `a curve can have up to ${MAX_POINTS} points`;
  if (points.some(([v, o]) => !Number.isInteger(v) || v < 1 || v > 127 || !Number.isFinite(o) || o < k.min || o > k.max ||
      Math.round(o / k.step) * k.step !== o)) {
    return kind === "duty"
      ? "each point needs a velocity from 1 to 127 and a duty change from -50 to 50, in 0.25 steps"
      : "each point needs a velocity from 1 to 127 and an offset from -60 to 60";
  }
  if (points.some((p, i) => i > 0 && p[0] <= points[i - 1][0])) return "the velocities must rise from point to point";
  if (kind === "velocity") {
    for (let v = 2; v <= 127; v++) {
      if (adjust(points, v) < adjust(points, v - 1)) {
        return `velocity ${v} would play softer than ${v - 1}: an offset can't fall by more than 1 per velocity`;
      }
    }
  }
  return "";
}

// What it can't do as asked (shown, but it can be saved): velocities pushed
// past 1 or 127, duties past 0 or 100%, a velocity struck with less duty
// than the one below it.
function limits(info) {
  const offsets = pointsShown(info, "velocity"), duty = pointsShown(info, "duty"), table = tableShown(info);
  const over = [], under = [], full = [], falls = [];
  for (let v = 1; v <= 127; v++) {
    const want = v + offsetAt(offsets, v);
    if (want > 127) over.push(v);
    if (want < 1) under.push(v);
    if (table[adjust(offsets, v) - 1] + dutyChangeAt(duty, v) > 100) full.push(v);
  }
  const played = playedDuties(info);
  for (let v = 2; v <= 127; v++) if (played[v - 1] < played[v - 2]) falls.push(v);
  return [
    over.length ? `velocities ${span(over)} already play at 127: no louder by offset` : "",
    under.length ? `velocities ${span(under)} already play at 1: no softer by offset` : "",
    full.length ? `velocities ${span(full)} are held at 100% duty` : "",
    falls.length ? `velocit${falls.length > 1 ? "ies" : "y"} ${falls.length > 3 ? span(falls) : falls.join(", ")} ` +
      `get${falls.length > 1 ? "" : "s"} less duty than the velocity below` : "",
  ].filter(Boolean).join("; ");
}

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
    box.textContent = "No key is adjusted yet: adjusted keys get an orange dot on the keyboard.";
    return;
  }
  const what = (a) => [a.up?.length || a.pedal ? "offsets" : "", a.duty || a.duty_pedal ? "duty" : "",
    a.pedal || a.duty_pedal ? "own pedal-down" : ""].filter(Boolean).join(", ");
  box.replaceChildren("Adjusted keys:", ...keys.map((key) => ui.el("button", { class: "btn btn-sm", type: "button",
    title: what(allOffsets[key]), onclick: () => selectKey(key) },
  noteName(key), ui.el("span", { class: "muted" }, ` ${what(allOffsets[key])}`))));
  const clear = ui.el("button", { class: "btn btn-ghost btn-sm danger-text", type: "button" }, "Clear all");
  clear.addEventListener("click", async () => {
    const sure = await ui.confirm({ title: "Clear every key's adjustments?",
      message: "Every key's offsets and duty changes go: they all play as the velocity table says.",
      confirm: "Clear all", danger: true });
    if (!sure) return;
    if (await post("/api/offsets/clear", undefined, "every key's adjustments cleared")) {
      draft = null;
      draftFor = "";
      await loadOffsets();
      loadKeyInfo();
    }
  });
  box.append(clear);
}

function pointRow(point) {
  const k = KINDS[editKind()];
  const tr = document.createElement("tr");
  point.forEach((value, i) => {
    const td = document.createElement("td");
    const input = document.createElement("input");
    input.className = "input input-sm";
    Object.assign(input, { type: "number", value, ...(i ? { min: k.min, max: k.max, step: k.step } : { min: 1, max: 127, step: 1 }) });
    input.setAttribute("aria-label", i ? k.label : "from velocity");
    input.addEventListener("input", () => { readDraft(); drawKeyInfo(false); });
    if (!i) {
      input.addEventListener("change", () => {
        const rows = [...$("points").querySelectorAll("tr")];
        const sorted = rows.every((r, n) => n === 0 || Number(rows[n - 1].querySelector("input").value) <= Number(r.querySelector("input").value));
        if (!sorted) sortRows(Number(input.value)); // the point moved: on to its value
        drawKeyInfo(false);
      });
    }
    td.append(input);
    tr.append(td);
  });
  const remove = document.createElement("button");
  remove.type = "button";
  remove.className = "btn btn-ghost btn-icon btn-sm";
  remove.innerHTML = ui.icon("close");
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

const unsaved = () => keyInfo && draftFor === draftKey(keyInfo) &&
  JSON.stringify(draft) !== JSON.stringify(savedCurve(keyInfo) ?? null);

// The editor, for the key shown; rows rebuilt only when asked (not while typing).
function drawOffsets(rebuild) {
  const box = $("offsets");
  box.hidden = !velocityMode() || !keyInfo;
  if (box.hidden) return;
  const info = keyInfo, kind = editKind(), k = KINDS[kind];
  const pedalDown = editingPedal();
  const own = pedalDown && info.offsets[k.pedal] !== null;
  const which = draftKey(info);
  if (rebuild || draftFor !== which) {
    draftFor = which;
    draft = pedalDown && !own ? null : (savedCurve(info) || []).map((p) => p.slice());
    $("points").replaceChildren(...(draft || []).map(pointRow));
  }
  document.querySelectorAll('input[name="adjust-kind"]').forEach((r) => (r.checked = r.value === kind));
  document.querySelectorAll(".adjust-kind [data-set]").forEach((dot) => {
    const points = pointsShown(info, dot.dataset.set);
    dot.hidden = !points.length || points.every((p) => p[1] === 0);
  });
  $("offsets-title").textContent = `Adjust ${noteName(info.key)}${pedalDown ? ", pedal down" : settings.pedal ? "" : ", pedal up"}`;
  $("point-value").textContent = k.label;
  $("own-pedal-row").hidden = !pedalDown;
  $("own-pedal-label").textContent = `Its own ${k.name} with the pedal down`;
  $("own-pedal").checked = own || draft !== null && pedalDown;
  $("offsets-edit").hidden = pedalDown && draft === null;
  $("offsets-hint").textContent = pedalDown && draft === null
    ? `With the pedal down it uses its pedal-up ${k.name}.`
    : (settings.pedal && !info.hasPedal ? `It has no pedal-down entry, so these apply with the pedal up and down. ` : "") + k.hint;
  const problem = draft ? curveProblem(draft, kind) : "";
  const message = $("offsets-message");
  const changed = unsaved();
  message.className = problem ? "error" : "";
  message.textContent = problem || (changed ? "not saved yet: the curve shows it" : "");
  $("save-offsets").disabled = Boolean(problem) || !changed;
  $("add-point").disabled = (draft || []).length >= MAX_POINTS;
  $("add-point").textContent = (draft || []).some((p) => p[0] === settings.velocity)
    ? "Add a point" : `Add a point at ${settings.velocity}`;
  $("clear-offsets").textContent = pedalDown ? `Use the pedal-up ${k.name}` : `Remove its ${k.name}`;
}

// Where a new point goes: at the velocity being played, else (one's there
// already) in the middle of the widest gap. Its value is what the curve
// gives there already, so adding it changes nothing until it's edited.
function newPoint(points) {
  const at = (v) => (editKind() === "duty" ? dutyChangeAt(points, v) : offsetAt(points, v));
  const taken = new Set(points.map((p) => p[0]));
  let v = settings.velocity;
  if (taken.has(v)) {
    const edges = [0, ...points.map((p) => p[0]).sort((a, b) => a - b), 128];
    let best = 0;
    for (let i = 1; i < edges.length; i++) {
      if (edges[i] - edges[i - 1] > edges[best + 1] - edges[best]) best = i - 1;
    }
    v = Math.min(127, Math.max(1, Math.round((edges[best] + edges[best + 1]) / 2)));
    if (taken.has(v)) return null; // no room
  }
  return [v, at(v)];
}

// The rows in order of velocity (once one's been typed in), then on to the
// value of the point at focusVelocity.
function sortRows(focusVelocity) {
  readDraft();
  const sorted = draft.every((p, i) => i === 0 || draft[i - 1][0] <= p[0]);
  if (sorted && focusVelocity === undefined) return;
  draft.sort((a, b) => a[0] - b[0]);
  $("points").replaceChildren(...draft.map(pointRow));
  if (focusVelocity !== undefined) {
    const row = draft.findIndex((p) => p[0] === focusVelocity);
    $("points").querySelectorAll("tr")[row]?.querySelectorAll("input")[1]?.focus();
  }
}

$("add-point").addEventListener("click", () => {
  readDraft();
  const point = newPoint(draft);
  if (!point) return;
  draft.push(point);
  $("points").replaceChildren(...draft.map(pointRow));
  sortRows(point[0]);
  drawKeyInfo(false);
});

$("own-pedal").addEventListener("change", () => {
  // Starting from the pedal-up ones.
  draft = $("own-pedal").checked ? (keyInfo.offsets[KINDS[editKind()].up] || []).map((p) => p.slice()) : null;
  $("points").replaceChildren(...(draft || []).map(pointRow));
  drawKeyInfo(false);
});

document.querySelectorAll('input[name="adjust-kind"]').forEach((radio) => radio.addEventListener("change", async () => {
  if (unsaved() && !await ui.confirm({ title: "Discard the changes?",
    message: `The ${KINDS[editKind()].name} you've changed aren't saved.`, confirm: "Discard", danger: true })) {
    radio.checked = false;
    document.querySelector(`input[name="adjust-kind"][value="${editKind()}"]`).checked = true;
    return;
  }
  settings.adjustKind = radio.value;
  save();
  drawKeyInfo(true);
}));

async function saveOffsets(points) {
  const info = keyInfo, kind = editKind(), k = KINDS[kind];
  const o = info.offsets;
  const body = { key: info.key, up: o.up, pedal: o.pedal, duty: o.duty, duty_pedal: o.dutyPedal };
  if (editingPedal()) body[k.apiPedal] = points;
  else body[k.up] = points || [];
  const reply = await post("/api/offsets/key", body,
    `${noteName(info.key)} ${k.name}${editingPedal() ? " (pedal down)" : ""}: ` +
    (points === null ? "same as pedal up" : points.length ? points.map(([v, x]) => `${v}: ${k.format(x)}`).join(", ") : "none"));
  if (reply) {
    allOffsets = reply.keys || {};
    showAdjustedKeys();
    await loadKeyInfo(true);
  }
}
$("save-offsets").addEventListener("click", () => saveOffsets(draft));
$("clear-offsets").addEventListener("click", () => saveOffsets(editingPedal() ? null : []));
