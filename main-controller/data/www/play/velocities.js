// The piece's velocities as written and with the dynamics settings (the
// chart under the sliders), shaped as the controller shapes them. Uses
// settings() from app.js.
const velocitySection = document.getElementById("velocities");
const velocityChart = document.getElementById("velocity-chart");
const velocityLow = document.getElementById("velocity-low");
let pieceNotes = null; // [{on, velocity, part}] as written, from the note view
const BIN = 6; // velocities per bar (narrower bars zig-zag as the settings round velocities)
const CHORD_WINDOW_MS = 40; // as roll_builder.h's CHORD_WINDOW_MS
const LOW_VELOCITY = 20; // below this, a few keys can be weak or miss

// As the controller shapes them (roll_builder.cpp shapeVelocities): each
// part's offset, then voicing within each chord, then contrast around the
// piece's median, then volume; rounded once and kept within 1-127. notes are
// in the order they're written.
function shapeVelocities(notes, s) {
  const shaped = notes.map((n) => n.velocity + (s.parts[n.part] || 0));
  if (s.voicing !== 100) {
    for (let first = 0; first < notes.length; ) {
      let end = first + 1;
      while (end < notes.length && notes[end].on - notes[first].on < CHORD_WINDOW_MS) end++;
      if (end - first > 1) {
        let average = 0;
        for (let i = first; i < end; i++) average += shaped[i];
        average /= end - first;
        for (let i = first; i < end; i++) {
          shaped[i] = average + ((shaped[i] - average) * s.voicing) / 100;
        }
      }
      first = end;
    }
  }
  if (s.contrast !== 100 && shaped.length) {
    const sorted = shaped.slice().sort((a, b) => a - b);
    const middle = Math.floor(sorted.length / 2);
    const median = sorted.length % 2 ? sorted[middle] : (sorted[middle - 1] + sorted[middle]) / 2;
    for (let i = 0; i < shaped.length; i++) {
      shaped[i] = median + ((shaped[i] - median) * s.contrast) / 100;
    }
  }
  return shaped.map((v) => Math.min(127, Math.max(1, Math.floor((v * s.volume + 50) / 100))));
}

function histogram(velocities) {
  const counts = new Array(127).fill(0);
  velocities.forEach((v) => counts[v - 1]++);
  return counts;
}

// The notes the controller shapes, from the note view's list: a note the file
// has twice at once is shaped once, at the louder of the two velocities.
function shapedNotes(notes) {
  const DUPLICATE = 32, MERGED = 1, MUTED = 128;
  const twin = (n) => (n.changes & DUPLICATE) || ((n.changes & MERGED) && n.mergedInto === n.on);
  const louder = new Map();
  notes.filter(twin).forEach((n) => {
    const key = n.key + "/" + n.on;
    louder.set(key, Math.max(louder.get(key) || 0, n.writtenVelocity));
  });
  return notes
    .filter((n) => !twin(n) && !(n.changes & MUTED)) // muted parts aren't played
    .map((n) => ({
      on: n.on,
      velocity: Math.max(n.writtenVelocity, louder.get(n.key + "/" + n.on) || 0),
      part: n.part,
    }));
}

function describe(counts) {
  let lowest = 0,
    highest = 0;
  counts.forEach((count, i) => {
    if (!count) return;
    if (!lowest) lowest = i + 1;
    highest = i + 1;
  });
  return { lowest, highest };
}

function bins(counts) {
  const out = [];
  for (let start = 1; start <= 127; start += BIN) {
    out.push({
      from: start,
      to: Math.min(start + BIN - 1, 127),
      count: counts
        .slice(start - 1, start - 1 + BIN)
        .reduce((a, b) => a + b, 0),
    });
  }
  return out;
}

const SVG = "http://www.w3.org/2000/svg";
function svgEl(name, attrs) {
  const el = document.createElementNS(SVG, name);
  Object.entries(attrs).forEach(([k, v]) => el.setAttribute(k, v));
  return el;
}

function drawVelocities() {
  if (!pieceNotes) return;
  const shaped = shapeVelocities(pieceNotes, settings());
  const written = histogram(pieceNotes.map((n) => n.velocity));
  const now = histogram(shaped);
  const a = describe(written),
    b = describe(now);
  const lowBefore = pieceNotes.filter((n) => n.velocity < LOW_VELOCITY).length;
  const lowNow = shaped.filter((v) => v < LOW_VELOCITY).length;
  velocityLow.textContent = lowNow
    ? `${lowNow} ${lowNow === 1 ? "note" : "notes"} below velocity ${LOW_VELOCITY}` +
      (lowNow !== lowBefore ? ` (${lowBefore} as written)` : "") +
      ": a few keys can be weak or miss down there"
    : "";
  const writtenBins = bins(written),
    playedBins = bins(now);

  const W = velocityChart.clientWidth || 600,
    H = 164,
    m = { left: 4, right: 4, top: 28, bottom: 20 };
  const slot = (W - m.left - m.right) / writtenBins.length;
  // The scale follows the piece as written, so its outlines stay put while the volume moves.
  // Bars taller than that are cut with a break and labelled with their count.
  const top = Math.max(
    1,
    Math.ceil(Math.max(...writtenBins.map((x) => x.count)) * 1.2),
  );
  const y = (count) =>
    m.top + (H - m.top - m.bottom) * (1 - Math.min(count, top) / top);
  const svg = svgEl("svg", {
    viewBox: `0 0 ${W} ${H}`,
    role: "img",
    "aria-label": `Notes by velocity: as written ${a.lowest} to ${a.highest}, with these settings ${b.lowest} to ${b.highest}`,
  });
  svg.append(
    svgEl("line", {
      x1: m.left,
      x2: W - m.right,
      y1: H - m.bottom,
      y2: H - m.bottom,
      stroke: ui.token("--chart-line"),
    }),
  );
  playedBins.forEach((bin, i) => {
    if (!bin.count) return;
    const h = H - m.bottom - y(bin.count);
    const x = m.left + i * slot + 1,
      width = Math.max(slot - 2, 1);
    svg.append(
      svgEl("rect", {
        x,
        y: y(bin.count),
        width,
        height: h,
        rx: Math.min(2, h / 2),
        fill: ui.token("--chart-played"),
      }),
    );
    if (bin.count > top) {
      svg.append(
        svgEl("rect", { x, y: m.top + 6, width, height: 3, fill: ui.token("--chart-bg") }),
      );
      const label = svgEl("text", {
        x: x + width / 2,
        y: m.top - 4,
        class: "tick",
        "text-anchor": "middle",
      });
      label.textContent = bin.count;
      svg.append(label);
    }
  });
  writtenBins.forEach((bin, i) => {
    if (!bin.count) return;
    svg.append(
      svgEl("rect", {
        x: m.left + i * slot + 1.75,
        y: y(bin.count),
        width: Math.max(slot - 3.5, 1),
        height: H - m.bottom - y(bin.count),
        fill: "none",
        stroke: ui.token("--chart-written"),
        "stroke-width": 1.5,
      }),
    );
  });
  [1, 32, 64, 96, 127].forEach((v) => {
    const x =
      m.left +
      ((v - 1) / 127) * (W - m.left - m.right) +
      (v === 127 ? slot / 2 : 0);
    const label = svgEl("text", {
      x,
      y: H - 4,
      class: "tick",
      "text-anchor": v === 1 ? "start" : v === 127 ? "end" : "middle",
    });
    label.textContent = v === 127 ? "127 (max)" : v;
    svg.append(label);
  });
  // A guide line at a round count, so bar heights can be read.
  const unit = 10 ** Math.floor(Math.log10(top * 0.9));
  const guide = Math.floor((top * 0.9) / unit) * unit;
  svg.append(
    svgEl("line", {
      x1: m.left,
      x2: W - m.right,
      y1: y(guide),
      y2: y(guide),
      stroke: ui.token("--chart-line"),
      "stroke-dasharray": "3 3",
    }),
  );
  const guideLabel = svgEl("text", {
    x: W - m.right,
    y: y(guide) - 4,
    class: "tick",
    "text-anchor": "end",
  });
  guideLabel.textContent = guide + " notes per bar";
  // A halo in the background colour, so it reads over a bar.
  guideLabel.style.cssText = `paint-order: stroke; stroke: ${ui.token("--chart-bg")}; stroke-width: 4px; stroke-linejoin: round;`;
  svg.append(guideLabel);

  const tip = document.createElement("div");
  tip.className = "tip";
  tip.hidden = true;
  writtenBins.forEach((bin, i) => {
    const hit = svgEl("rect", {
      x: m.left + i * slot,
      y: m.top,
      width: slot,
      height: H - m.top - m.bottom,
      fill: "transparent",
    });
    hit.addEventListener("pointerenter", () => {
      tip.textContent = `velocity ${bin.from}-${bin.to}: ${bin.count} as written, ${playedBins[i].count} with these settings`;
      tip.hidden = false;
      const x = m.left + i * slot + slot / 2;
      tip.style.left =
        Math.min(
          Math.max(0, x - tip.offsetWidth / 2),
          velocityChart.clientWidth - tip.offsetWidth,
        ) + "px";
      tip.style.top = "-6px";
    });
    hit.addEventListener("pointerleave", () => {
      tip.hidden = true;
    });
    svg.append(hit);
  });
  velocityChart.replaceChildren(svg, tip);
}

window.addEventListener("resize", drawVelocities);
// Drawn again at its new width (the layout changes with the screen), and in
// the other theme's colours.
new ResizeObserver(() => drawVelocities()).observe(velocityChart);
ui.onTheme(drawVelocities);
