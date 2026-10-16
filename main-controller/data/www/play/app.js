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
let volumeChanging = false; // while dragging, don't let refresh() move the slider
// The position bar: drag it to choose where Play starts, or to jump while playing.
let startFrom = 0; // ms on the piece's timeline where Play starts
let seeking = false; // while dragging, don't let refresh() move the bar
let playing = false;
let paused = false; // every key up; Play goes on from there
let durationMs = 0;

// ---- the piece's velocities, as written and at the slider's volume ----
const velocitySection = document.getElementById("velocities");
const velocityChart = document.getElementById("velocity-chart");
let written = null; // notes at each velocity 1-127, as written
let loadedPiece = "";
const BIN = 6; // velocities per bar (narrower bars zig-zag as the volume rounds velocities)

// As the controller scales them (roll_builder.cpp scaleVelocities).
function scaled(velocity, percent) {
  return Math.min(
    127,
    Math.max(1, Math.floor((velocity * percent + 50) / 100)),
  );
}

function played(percent) {
  const counts = new Array(127).fill(0);
  written.forEach((count, i) => {
    counts[scaled(i + 1, percent) - 1] += count;
  });
  return counts;
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
  if (!written) return;
  const percent = Number(volume.value);
  const now = played(percent);
  const a = describe(written),
    b = describe(now);
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
    "aria-label": `Notes by velocity: as written ${a.lowest} to ${a.highest}, at ${percent}% ${b.lowest} to ${b.highest}`,
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
      tip.textContent = `velocity ${bin.from}-${bin.to}: ${bin.count} as written, ${playedBins[i].count} at ${percent}%`;
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

async function loadVelocities() {
  try {
    const response = await fetch("/api/piece");
    written = response.ok ? (await response.json()).velocities : null;
  } catch (error) {
    written = null;
  }
  velocitySection.hidden = !written;
  drawVelocities();
}
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

// Every velocity is scaled by the volume; it applies from the next Play.
let volumeTimer;
volume.addEventListener("input", () => {
  volumeChanging = true;
  volumeValue.textContent = volume.value + "%";
  drawVelocities();
  clearTimeout(volumeTimer);
  volumeTimer = setTimeout(async () => {
    const saved = await request("/api/volume", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ volume: Number(volume.value) }),
    });
    volumeChanging = false;
    if (saved) {
      noteView.load(); // merges and limits depend on the volume
      volumeNote.textContent = playing
        ? "applies from the next Play, or after a pause"
        : "";
    }
  }, 300);
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
    noteView.load(); // shows the current volume again, if it was changed while playing
  }
  if (
    !volumeChanging &&
    status.volume !== undefined &&
    Number(volume.value) !== status.volume
  ) {
    volume.value = status.volume;
    volumeValue.textContent = status.volume + "%";
    drawVelocities();
  }
  const pieceKey = playback.piece
    ? playback.piece.title + "/" + playback.piece.notes
    : "";
  if (pieceKey !== loadedPiece) {
    loadedPiece = pieceKey;
    startFrom = 0;
    loadVelocities();
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
