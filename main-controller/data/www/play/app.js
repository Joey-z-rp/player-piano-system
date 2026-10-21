// The play page: loading and playing a piece, the dynamics sliders and
// tracks, and keeping it all in step with the controller (refresh). The note
// view, velocity chart and library are in notes.js, velocities.js and
// library.js.
const fileInput = document.getElementById("file");
const piece = document.getElementById("piece");
const playButton = document.getElementById("play");
const pauseButton = document.getElementById("pause");
const stopButton = document.getElementById("stop");
const progress = document.getElementById("progress");
const time = document.getElementById("time");
const message = document.getElementById("message");
const busy = document.getElementById("busy");
const pieceMeta = document.getElementById("piece-meta");

// A message floats at the bottom for a while (an error a little longer), or
// until it's clicked.
let messageTimer;
new MutationObserver(() => {
  clearTimeout(messageTimer);
  if (message.textContent) {
    messageTimer = setTimeout(() => (message.textContent = ""), message.classList.contains("error") ? 12000 : 7000);
  }
}).observe(message, { childList: true, characterData: true, subtree: true });
message.addEventListener("click", () => (message.textContent = ""));
const notesBusy = document.getElementById("notes-busy");
const notesSection = document.getElementById("notes");
const volume = document.getElementById("volume");
const volumeValue = document.getElementById("volume-value");
const volumeNote = document.getElementById("volume-note");
const contrast = document.getElementById("contrast");
const contrastValue = document.getElementById("contrast-value");
const voicing = document.getElementById("voicing");
const voicingValue = document.getElementById("voicing-value");
const speed = document.getElementById("speed");
const speedValue = document.getElementById("speed-value");
const repeatBoost = document.getElementById("repeat-boost");
const repeatBoostValue = document.getElementById("repeat-boost-value");
const repeatNote = document.getElementById("repeat-note");
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
// What's being built: by the controller (status "building"), for an upload,
// or the note view's notes. One indicator shows the first that applies.
let controllerBuilding = false;
let libraryBusy = false; // playing or paused: no loading another piece
let uploading = false;
let notesLoading = false;
// Over the note view when it's showing, else beside the piece's name.
function showBusy() {
  const text = uploading ? "Uploading and building the piece…"
    : controllerBuilding ? "Building the piece with the new settings…"
    : notesLoading ? "Updating the notes…" : "";
  const onNotes = !notesSection.hidden;
  notesBusy.firstElementChild.textContent = text;
  notesBusy.hidden = !text || !onNotes;
  busy.textContent = text;
  busy.hidden = !text || onNotes;
  showButtons();
}

// Play waits while the piece is being built or the settings are on their way.
let hasPiece = false;
function showButtons() {
  fileInput.disabled = playing || paused || uploading;
  playButton.disabled = playing || !hasPiece || controllerBuilding || notesLoading || uploading || dynamicsChanging;
  playButton.textContent = paused ? "Resume" : "Play";
  pauseButton.disabled = !playing;
  stopButton.disabled = !playing && !paused;
}
noteView.onLoading((loading) => {
  notesLoading = loading;
  showBusy();
});

// The speed the loaded piece is built at, which its times (the bar, startFrom)
// are in, and its lead (times before it don't scale): from /api/status.
let pieceSpeed = 100;
let pieceLead = 0;

let partInfo = []; // the piece's parts, from /api/piece
let partOffsets = []; // velocities added to each part
let partMuted = []; // parts left out
let loadedPiece = "";

function settings() {
  return {
    volume: Number(volume.value),
    contrast: Number(contrast.value),
    voicing: Number(voicing.value),
    repeatBoost: Number(repeatBoost.value),
    speed: Number(speed.value),
    parts: partOffsets.slice(),
    muted: partMuted.slice(),
  };
}

// The piece's parts, and their offsets, for the tracks list.
async function loadPiece() {
  let parts = [], offsets = [], muted = [];
  try {
    const [piece, dynamics] = await Promise.all([fetch("/api/piece"), fetch("/api/dynamics")]);
    parts = piece.ok ? (await piece.json()).parts || [] : [];
    const saved = dynamics.ok ? await dynamics.json() : {};
    offsets = saved.parts || [];
    muted = saved.muted || [];
  } catch (error) {
    parts = [];
  }
  partInfo = parts;
  partOffsets = parts.map((_, i) => offsets[i] || 0);
  partMuted = parts.map((_, i) => Boolean(muted[i]));
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
      // Mute: left out, as if the file didn't have it (never every part).
      const mute = document.createElement("button");
      mute.type = "button";
      mute.className = "mute";
      const showMute = () => {
        mute.textContent = partMuted[i] ? "Muted" : "Mute";
        mute.setAttribute("aria-pressed", String(partMuted[i]));
        row.classList.toggle("muted", partMuted[i]);
        slider.disabled = partMuted[i];
      };
      showMute();
      mute.addEventListener("click", () => {
        const playing = partInfo.filter((p, j) => p.notes > 0 && !partMuted[j] && j !== i).length;
        if (!partMuted[i] && playing === 0) {
          message.classList.remove("error");
          message.textContent = "Every other track is muted: unmute one first";
          return;
        }
        partMuted[i] = !partMuted[i];
        showMute();
        dynamicsChanged();
      });
      row.append(swatch, label, slider, value, mute);
      return row;
    }),
  );
}

// How many repeats are merged (not struck) and boosted, as the controller
// will play the piece: from the note view's notes, built with the settings.
function showRepeats(notes) {
  if (!notes) {
    repeatNote.textContent = "";
    return;
  }
  const MERGED = 1, BOOSTED = 64;
  const merged = notes.filter((n) => (n.changes & MERGED) && n.mergedInto !== n.on).length;
  const boosted = notes.filter((n) => n.changes & BOOSTED).length;
  repeatNote.textContent = `${merged} repeated ${merged === 1 ? "note" : "notes"} merged (too soon to strike again)` +
    (boosted ? `; ${boosted} played louder to strike again` : "");
}

noteView.onLoaded((notes) => {
  showRepeats(notes);
  pieceNotes = notes ? shapedNotes(notes) : null;
  velocitySection.hidden = !pieceNotes;
  drawVelocities();
});

function formatTime(ms) {
  const seconds = Math.floor(ms / 1000);
  return Math.floor(seconds / 60) + ":" + String(seconds % 60).padStart(2, "0");
}

async function request(url, options) {
  try {
    const response = await fetch(url, options);
    const body = await response.json();
    if (!response.ok) {
      // (A success leaves any message to fade: it may be about something else.)
      message.textContent = body.error;
      message.classList.add("error");
    }
    return response.ok ? body : null;
  } catch (error) {
    message.textContent = "request failed: " + error.message;
    message.classList.add("error");
    return null;
  }
}

fileInput.addEventListener("change", async () => {
  const file = fileInput.files[0];
  if (!file) {
    return;
  }
  message.textContent = "";
  message.classList.remove("error");
  uploading = true;
  showBusy();
  const summary = await request(
    "/api/midi?name=" + encodeURIComponent(file.name),
    {
      method: "POST",
      headers: { "Content-Type": "application/octet-stream" },
      body: file,
    },
  );
  uploading = false;
  showBusy();
  loadLibrary();
  if (!summary) {
    message.textContent = `Couldn't load ${file.name}: ${message.textContent}`;
  } else {
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
    const saved = {
      saved: "Saved in the library",
      already: `Already in the library as “${summary.title}”: loaded with its settings`,
      full: "The library is full, so it wasn't saved (delete some pieces to make room)",
      unavailable: "Not saved: the controller has no library space (upload the firmware with the new partition table)",
      failed: "Couldn't save it in the library",
    }[summary.library];
    if (saved) notices.unshift(saved);
    message.textContent = notices.join("; ");
  }
  fileInput.value = "";
  refresh();
});

playButton.addEventListener("click", async () => {
  // Paused, it goes on from where it stopped (or from where the bar was moved to).
  const from = !paused && startFrom > 0 ? `?from=${startFrom}&speed=${pieceSpeed}` : "";
  await request("/api/playback/play" + from, { method: "POST" });
  refresh();
  loadLibrary(); // its last played
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
    await request(`/api/playback/seek?to=${ms}&speed=${pieceSpeed}`, { method: "POST" });
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
// they apply from the next Play. A new speed applies at once when nothing's
// playing (the controller rebuilds the piece), else after a pause.
function showDynamics() {
  speedValue.textContent = speed.value + "%";
  volumeValue.textContent = volume.value + "%";
  contrastValue.textContent = contrast.value + "%";
  voicingValue.textContent = voicing.value + "%";
  repeatBoostValue.textContent = "+" + repeatBoost.value;
}

let dynamicsTimer;
function dynamicsChanged() {
  dynamicsChanging = true;
  showButtons();
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
    showButtons();
    if (saved) {
      noteView.load(); // merges and limits depend on the velocities
      volumeNote.textContent = playing ? "applies from the next Play, or after a pause" : "";
    }
  }, 300);
}

[speed, volume, contrast, voicing, repeatBoost].forEach((slider) => slider.addEventListener("input", dynamicsChanged));

resetButton.addEventListener("click", () => {
  volume.value = 100;
  speed.value = 100;
  contrast.value = 100;
  voicing.value = 100;
  repeatBoost.value = 0;
  partOffsets = partOffsets.map(() => 0);
  partMuted = partMuted.map(() => false);
  showParts();
  dynamicsChanged();
});

// The solenoids' heat, when the controller watches it (their coils measured).
const heatLine = document.getElementById("heat");
function showHeat(heat) {
  heatLine.hidden = !heat || !heat.watched;
  if (heatLine.hidden) return;
  const name = (channel) => (channel >= 88 ? "the pedal solenoids" : `${noteName(channel + 21)}'s solenoid`);
  const minutes = (s) => (s < 60 ? "under a minute" : `about ${Math.ceil(s / 60)} min`);
  const degrees = (c) => `${c.toFixed(0)} °C`;
  const cooling = heat.load >= heat.resumeLoad;
  const stopped = { paused: "Paused", testStopped: "Test stopped", released: "Let go" }[heat.action];
  heatLine.className = "heat" + (stopped && cooling ? " hot" : cooling ? " warm" : "");
  if (stopped && cooling) {
    heatLine.textContent = `${stopped}: ${name(heat.actionChannel)} reached the heat limit. ` +
      `Play can go on once ${heat.actionChannel >= 88 ? "they've" : "it's"} cooled, in ${minutes(heat.coolS)}.`;
  } else if (cooling) {
    heatLine.textContent = `Warm: ${name(heat.channel)} at ${degrees(heat.riseC)} of the ${degrees(heat.limitC)} ` +
      `allowed. Play waits until under ${degrees(heat.limitC * heat.resumeLoad)}.`;
  } else {
    heatLine.textContent = `Solenoid heat: warmest is ${name(heat.channel)}, ${degrees(heat.riseC)} of ` +
      `${degrees(heat.limitC)} allowed`;
  }
}

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
  showHeat(status.heat);
  controllerBuilding = Boolean(playback.building);
  showBusy();
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
    const now = [Number(volume.value), Number(contrast.value), Number(voicing.value), Number(repeatBoost.value),
      Number(speed.value)];
    const saved = [status.volume, status.contrast ?? 100, status.voicing ?? 100, status.repeatBoost ?? 0,
      status.speed ?? 100];
    if (now.some((v, i) => v !== saved[i])) {
      [volume.value, contrast.value, voicing.value, repeatBoost.value, speed.value] = saved;
      showDynamics();
      drawVelocities();
    }
  }
  const pieceKey = playback.piece ? String(playback.piece.id ?? playback.piece.title) : "";
  const builtAt = playback.piece?.speed ?? 100;
  if (pieceKey === loadedPiece && builtAt !== pieceSpeed) {
    // Rebuilt for a new speed: Play still starts from the same point in the music.
    if (!playing && !paused && startFrom > pieceLead) {
      startFrom = Math.round(pieceLead + ((startFrom - pieceLead) * pieceSpeed) / builtAt);
    }
    noteView.load();
  }
  pieceSpeed = builtAt;
  pieceLead = playback.piece?.leadMs ?? 0;
  if (pieceKey !== loadedPiece) {
    loadedPiece = pieceKey;
    startFrom = 0;
    pieceNotes = null;
    loadPiece();
    noteView.load(true); // a new piece: its notes from the start
  }
  if (!playing) {
    volumeNote.textContent = "";
  }

  const libraryId = playback.piece?.libraryId || "";
  const wasBusy = libraryBusy;
  libraryBusy = playing || paused || uploading;
  if (libraryId !== loadedLibraryId || wasBusy !== libraryBusy) {
    loadedLibraryId = libraryId;
    drawLibrary();
  }
  if (playback.piece) {
    const saved = libraryPieces.find((p) => p.id === libraryId);
    piece.textContent = saved ? saved.name : playback.piece.title;
    pieceMeta.textContent = `${playback.piece.notes.toLocaleString()} notes · ${formatTime(playback.piece.durationMs)}`;
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
    pieceMeta.textContent = "Pick one from the library, or add one.";
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

  hasPiece = Boolean(playback.piece);
  showButtons();
}

refresh();
loadLibrary();
setInterval(refresh, 500);
