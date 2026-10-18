const fileInput = document.getElementById("file");
const piece = document.getElementById("piece");
const playButton = document.getElementById("play");
const pauseButton = document.getElementById("pause");
const stopButton = document.getElementById("stop");
const progress = document.getElementById("progress");
const time = document.getElementById("time");
const message = document.getElementById("message");
const volume = document.getElementById("volume");
const volumeValue = document.getElementById("volume-value");
const volumeNote = document.getElementById("volume-note");
const contrast = document.getElementById("contrast");
const contrastValue = document.getElementById("contrast-value");
const voicing = document.getElementById("voicing");
const voicingValue = document.getElementById("voicing-value");
const partsSection = document.getElementById("parts");
const partsList = document.getElementById("parts-list");
const resetButton = document.getElementById("dynamics-reset");
let dynamicsChanging = false; // while changing them, don't let refresh() move the sliders
// The position bar: drag it to choose where Play starts, or to jump while playing.
let startFrom = 0; // ms on the piece's timeline where Play starts
let seeking = false; // while dragging, don't let refresh() move the bar
let playing = false;
let paused = false; // every key up; Play goes on from there
let durationMs = 0;

// ---- the piece's velocities, as written and with the dynamics settings ----
const velocitySection = document.getElementById("velocities");
const velocityChart = document.getElementById("velocity-chart");
const velocityLow = document.getElementById("velocity-low");
let pieceNotes = null; // [{on, velocity, part}] as written, from the note view
let partInfo = []; // the piece's parts, from /api/piece
let partOffsets = []; // velocities added to each part
let loadedPiece = "";
const BIN = 6; // velocities per bar (narrower bars zig-zag as the settings round velocities)
const CHORD_WINDOW_MS = 40; // as roll_builder.h's CHORD_WINDOW_MS
const LOW_VELOCITY = 20; // below this, a few keys can be weak or miss

function settings() {
  return {
    volume: Number(volume.value),
    contrast: Number(contrast.value),
    voicing: Number(voicing.value),
    parts: partOffsets.slice(),
  };
}

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
  const DUPLICATE = 32, MERGED = 1;
  const twin = (n) => (n.changes & DUPLICATE) || ((n.changes & MERGED) && n.mergedInto === n.on);
  const louder = new Map();
  notes.filter(twin).forEach((n) => {
    const key = n.key + "/" + n.on;
    louder.set(key, Math.max(louder.get(key) || 0, n.writtenVelocity));
  });
  return notes
    .filter((n) => !twin(n))
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
      stroke: "#e1e0d9",
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
        fill: "#2a78d6",
      }),
    );
    if (bin.count > top) {
      svg.append(
        svgEl("rect", { x, y: m.top + 6, width, height: 3, fill: "#fff" }),
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
        stroke: "#8a8984",
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
      stroke: "#e1e0d9",
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

// The piece's parts, and their offsets, for the tracks list.
async function loadPiece() {
  let parts = [], offsets = [];
  try {
    const [piece, dynamics] = await Promise.all([fetch("/api/piece"), fetch("/api/dynamics")]);
    parts = piece.ok ? (await piece.json()).parts || [] : [];
    offsets = dynamics.ok ? (await dynamics.json()).parts || [] : [];
  } catch (error) {
    parts = [];
  }
  partInfo = parts;
  partOffsets = parts.map((_, i) => offsets[i] || 0);
  showParts();
  drawVelocities();
}

const NAMES = ["C", "C♯", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B"];
const noteName = (midi) => NAMES[midi % 12] + (Math.floor(midi / 12) - 1);

// One row per part, when the file has more than one, each with its offset.
function showParts() {
  const shown = partInfo.filter((p) => p.notes > 0).length > 1;
  partsSection.hidden = !shown;
  noteView.setPartCount(shown ? partInfo.length : 0);
  if (!shown) return;
  partsList.replaceChildren(
    ...partInfo.map((part, i) => {
      const row = document.createElement("div");
      row.className = "part";
      const swatch = document.createElement("i");
      swatch.className = "swatch";
      swatch.style.borderColor = noteView.partColor(i);
      const label = document.createElement("span");
      label.className = "part-name";
      label.textContent = part.notes
        ? `${part.name}: ${part.notes} notes, ${noteName(part.low)}-${noteName(part.high)}, typically ${part.velocity}`
        : `${part.name}: no notes on the keys`;
      const slider = document.createElement("input");
      Object.assign(slider, { type: "range", min: -20, max: 20, step: 1, value: partOffsets[i] });
      slider.setAttribute("aria-label", part.name + " offset");
      const value = document.createElement("output");
      const show = () => (value.textContent = (partOffsets[i] > 0 ? "+" : "") + partOffsets[i]);
      show();
      slider.addEventListener("input", () => {
        partOffsets[i] = Number(slider.value);
        show();
        dynamicsChanged();
      });
      row.append(swatch, label, slider, value);
      return row;
    }),
  );
}

noteView.onLoaded((notes) => {
  pieceNotes = notes ? shapedNotes(notes) : null;
  velocitySection.hidden = !pieceNotes;
  drawVelocities();
});
window.addEventListener("resize", drawVelocities);

function formatTime(ms) {
  const seconds = Math.floor(ms / 1000);
  return Math.floor(seconds / 60) + ":" + String(seconds % 60).padStart(2, "0");
}

async function request(url, options) {
  try {
    const response = await fetch(url, options);
    const body = await response.json();
    message.textContent = response.ok ? "" : body.error;
    return response.ok ? body : null;
  } catch (error) {
    message.textContent = "request failed: " + error.message;
    return null;
  }
}

fileInput.addEventListener("change", async () => {
  const file = fileInput.files[0];
  if (!file) {
    return;
  }
  message.textContent = "Uploading...";
  const summary = await request(
    "/api/midi?name=" + encodeURIComponent(file.name),
    {
      method: "POST",
      headers: { "Content-Type": "application/octet-stream" },
      body: file,
    },
  );
  if (summary) {
    const notices = [];
    if (summary.skippedNotes > 0) {
      notices.push(
        summary.skippedNotes + " notes outside the 88 keys were skipped",
      );
    }
    if (summary.mergedNotes > 0) {
      notices.push(
        summary.mergedNotes +
          " repeated notes were too fast to re-strike and were held instead",
      );
    }
    if (summary.duplicateNotes > 0) {
      notices.push(
        summary.duplicateNotes + " notes the file has twice at once were played once",
      );
    }
    message.textContent = notices.join("; ");
  }
  fileInput.value = "";
  refresh();
});

playButton.addEventListener("click", async () => {
  // Paused, it goes on from where it stopped (or from where the bar was moved to).
  const from = !paused && startFrom > 0 ? "?from=" + startFrom : "";
  await request("/api/playback/play" + from, { method: "POST" });
  refresh();
});

pauseButton.addEventListener("click", async () => {
  await request("/api/playback/pause", { method: "POST" });
  refresh();
});

progress.addEventListener("input", () => {
  seeking = true;
  time.textContent =
    formatTime(Number(progress.value)) + " / " + formatTime(durationMs);
});

progress.addEventListener("change", () => goTo(Number(progress.value)));

// Clicking the notes picks a point too.
noteView.onPick((ms) => goTo(Math.min(ms, durationMs)));

async function goTo(ms) {
  if (playing || paused) {
    await request("/api/playback/seek?to=" + ms, { method: "POST" });
  } else {
    startFrom = ms;
  }
  seeking = false;
  refresh();
}

stopButton.addEventListener("click", async () => {
  await request("/api/playback/stop", { method: "POST" });
  refresh();
});

// Volume, contrast, voicing and each part's offset shape every velocity;
// they apply from the next Play.
function showDynamics() {
  volumeValue.textContent = volume.value + "%";
  contrastValue.textContent = contrast.value + "%";
  voicingValue.textContent = voicing.value + "%";
}

let dynamicsTimer;
function dynamicsChanged() {
  dynamicsChanging = true;
  showDynamics();
  drawVelocities();
  clearTimeout(dynamicsTimer);
  dynamicsTimer = setTimeout(async () => {
    const saved = await request("/api/dynamics", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(settings()),
    });
    dynamicsChanging = false;
    if (saved) {
      noteView.load(); // merges and limits depend on the velocities
      volumeNote.textContent = playing ? "applies from the next Play, or after a pause" : "";
    }
  }, 300);
}

[volume, contrast, voicing].forEach((slider) => slider.addEventListener("input", dynamicsChanged));

resetButton.addEventListener("click", () => {
  volume.value = 100;
  contrast.value = 100;
  voicing.value = 100;
  partOffsets = partOffsets.map(() => 0);
  showParts();
  dynamicsChanged();
});

async function refresh() {
  let status, sampledAt;
  try {
    const sent = performance.now();
    const response = await fetch("/api/status");
    sampledAt = (sent + performance.now()) / 2; // about when the controller read its position
    status = await response.json();
  } catch (error) {
    return;
  }
  const playback = status.playback;
  const wasActive = playing || paused;
  const wasPlaying = playing;
  playing = playback.state === "playing";
  paused = playback.state === "paused";
  if (wasActive && !playing && !paused) {
    startFrom = 0; // stopped or finished: the next Play starts from the beginning
  }
  if (wasPlaying && !playing) {
    noteView.load(); // shows the current settings again, if they were changed while playing
  }
  if (!dynamicsChanging && status.volume !== undefined) {
    const now = [Number(volume.value), Number(contrast.value), Number(voicing.value)];
    const saved = [status.volume, status.contrast ?? 100, status.voicing ?? 100];
    if (now.some((v, i) => v !== saved[i])) {
      [volume.value, contrast.value, voicing.value] = saved;
      showDynamics();
      drawVelocities();
    }
  }
  const pieceKey = playback.piece
    ? playback.piece.title + "/" + playback.piece.notes
    : "";
  if (pieceKey !== loadedPiece) {
    loadedPiece = pieceKey;
    startFrom = 0;
    pieceNotes = null;
    loadPiece();
    noteView.load();
  }
  if (!playing) {
    volumeNote.textContent = "";
  }

  if (playback.piece) {
    piece.textContent =
      playback.piece.title +
      " (" +
      playback.piece.notes +
      " notes, " +
      formatTime(playback.piece.durationMs) +
      ")";
    durationMs = playback.piece.durationMs;
    progress.max = Math.max(durationMs, 1);
    if (!seeking) {
      const position =
        playing || paused
          ? Math.min(playback.positionMs, durationMs)
          : startFrom;
      progress.value = position;
      time.textContent = formatTime(position) + " / " + formatTime(durationMs);
    }
  } else {
    piece.textContent = "No piece loaded";
    time.textContent = "";
    durationMs = 0;
    progress.value = 0;
  }
  progress.disabled = !playback.piece;
  if (!seeking) {
    noteView.update(
      playing,
      playback.positionMs,
      paused ? playback.positionMs : startFrom,
      sampledAt,
    );
  }

  fileInput.disabled = playing || paused;
  playButton.disabled = playing || !playback.piece;
  playButton.textContent = paused ? "Resume" : "Play";
  pauseButton.disabled = !playing;
  stopButton.disabled = !playing && !paused;
}

refresh();
setInterval(refresh, 500);
