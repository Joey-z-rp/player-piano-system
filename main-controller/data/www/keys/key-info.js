// What the velocity table does with the selected key: its duty curve (with
// its adjustments, or the ones being edited in adjust.js), landing, clamp and
// the command it sends. Uses the helpers and settings in app.js, adjust.js,
// and ui (/assets/app.js).
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

// Its duty at each velocity, pedal up and (if it has its own) down.
function drawCurve(info) {
  const W = 460, H = 250, m = { left: 38, right: 8, top: 8, bottom: 22 };
  // The layer it plays from now: the table's curve, and (with adjustments,
  // or a draft of them) what it plays.
  const table = tableShown(info);
  const played = playedDuties(info);
  const adjusted = played.some((d, i) => d !== table[i]);
  // Zoomed to the key's own duties (to the 10% around them), so a change shows.
  const low = Math.max(0, Math.floor(Math.min(...table, ...played) / 10) * 10);
  const high = Math.min(100, Math.max(low + 10, Math.ceil(Math.max(...table, ...played) / 10) * 10));
  const x = (v) => m.left + ((v - 1) / 126) * (W - m.left - m.right);
  const y = (duty) => m.top + (1 - (duty - low) / (high - low)) * (H - m.top - m.bottom);
  const svg = svgEl("svg", { viewBox: `0 0 ${W} ${H}`, width: "100%", role: "img",
    "aria-label": `Duty by velocity for key ${info.key}: ${low}% to ${high}%` });
  svg.style.maxWidth = W + "px";
  for (let d = low; d <= high; d += 10) {
    svg.append(svgEl("line", { x1: m.left, x2: W - m.right, y1: y(d), y2: y(d), stroke: ui.token("--chart-line") }));
    const t = svgEl("text", { x: m.left - 5, y: y(d) + 3, class: "tick", "text-anchor": "end" });
    t.textContent = d + "%";
    svg.append(t);
  }
  [1, 32, 64, 96, 127].forEach((v) => {
    svg.append(svgEl("line", { x1: x(v), x2: x(v), y1: m.top, y2: H - m.bottom, stroke: ui.token("--chart-row") }));
    const t = svgEl("text", { x: x(v), y: H - 6, class: "tick", "text-anchor": v === 1 ? "start" : v === 127 ? "end" : "middle" });
    t.textContent = v;
    svg.append(t);
  });
  const line = (duties, colour, dash, width = 2) => svg.append(svgEl("polyline", {
    points: duties.map((d, i) => `${x(i + 1)},${y(d)}`).join(" "),
    fill: "none", stroke: colour, "stroke-width": width, ...(dash ? { "stroke-dasharray": "4 3" } : {}),
  }));
  if (adjusted) line(table, ui.token("--chart-written"), true, 1.5);
  line(played, ui.token("--chart-played"));
  const v = info.velocity;
  svg.append(svgEl("circle", { cx: x(v), cy: y(played[v - 1]), r: 5, fill: "#f2b33d", stroke: ui.token("--chart-bg"), "stroke-width": 2 }));
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
  const table = tableShown(info);
  const estimated = info.dutyChange && info.duty > table[126];
  lines.push(`At velocity ${info.velocity}${info.pedal && info.hasPedal ? " (pedal down)" : ""}` +
    (info.playedVelocity !== info.velocity ? `, played as ${info.playedVelocity} (its offset)` : "") +
    (info.dutyChange ? `, ${info.dutyChange > 0 ? "+" : ""}${info.dutyChange}% (its duty change)` : "") +
    `: duty ${info.duty}%` + (info.landingMs ? `, hammer lands ${info.landingMs} ms after the press` +
      (estimated ? " (estimated from its neighbours: louder than its calibration goes)" : "") : ""));
  lines.push(info.clamp
    ? `Clamp: ${info.clamp.duty}% for ${info.clamp.ms} ms from ${info.clamp.offsetMs} ms after it lands; then holds at ${info.hold}%`
    : `Coasts at ${info.coast}%, then holds at ${info.hold}%`);
  const played = playedDuties(info);
  const adjusted = played.some((d, i) => d !== table[i]);
  lines.push(`Duty from ${played[0]}% (velocity 1) to ${played[126]}% (127)` +
    (adjusted ? `; <span class="played-key">as played</span>, <span class="table-key">the table (dashed)</span>` : ""));
  const room = limits(info);
  if (room) lines.push(`<span class="room">${room}</span>`);
  lines.push(`Sends <code>${info.command}</code>`);
  facts.innerHTML = lines.join("<br>");
}

ui.onTheme(() => drawKeyInfo(false)); // the curve in the other theme's colours
