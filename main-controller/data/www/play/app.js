// The play page: loading and playing a piece, the sound sliders and tracks,
// and keeping it all in step with the controller's live status (ui.onStatus).
// The note view, velocity chart and library are in notes.js, velocities.js and
// library.js; the shared shell, messages and API in /assets/app.js.
const { formatTime, noteName, icon, el } = ui;
const fileInput = document.getElementById("file");
const piece = document.getElementById("piece");
const pieceMeta = document.getElementById("piece-meta");
const playButton = document.getElementById("play");
const stopButton = document.getElementById("stop");
const progress = document.getElementById("progress");
const timeNow = document.getElementById("time-now");
const timeTotal = document.getElementById("time-total");
const stateChip = document.getElementById("state-chip");
const busy = document.getElementById("busy");
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
const heatLine = document.getElementById("heat");

// Icons in the static buttons.
stopButton.innerHTML = icon("stop");
document.getElementById("zoom-in").innerHTML = icon("plus");
document.getElementById("zoom-out").innerHTML = icon("minus");
document.getElementById("change-prev").innerHTML = icon("left");
document.getElementById("change-next").innerHTML = icon("right");
document.getElementById("library-open").innerHTML = icon("library") + "<span>Library</span>";
document.getElementById("library-close").innerHTML = icon("close");
document.querySelector("#add-piece .add-icon").innerHTML = icon("plus");
document.querySelector(".search-icon").innerHTML = icon("search");

let dynamicsChanging = false; // while changing them, the status doesn't move the sliders
// The position bar: drag it to choose where Play starts, or to jump while playing.
let startFrom = 0; // ms on the piece's timeline where Play starts
let seeking = false; // while dragging, the status doesn't move the bar
let playing = false;
let paused = false; // every key up; Play goes on from there
let durationMs = 0;
// What's being built: by the controller (status "building"), for an upload,
// or the note view's notes. One indicator shows the first that applies.
let controllerBuilding = false;
let libraryBusy = false; // playing or paused: no loading another piece
let uploading = false;
let notesLoading = false;
// Over the note view when it's showing, else in the player.
function showBusy() {
  const text = uploading ? "Loading the piece…"
    : controllerBuilding ? "Building the piece with the new settings…"
    : notesLoading ? "Updating the notes…" : "";
  const onNotes = !notesSection.hidden;
  notesBusy.querySelector("span:last-child").textContent = text;
  notesBusy.hidden = !text || !onNotes;
  busy.querySelector("span:last-child").textContent = text;
  busy.hidden = !text || onNotes;
  showButtons();
}

// Play waits while the piece is being built or the settings are on their way.
let hasPiece = false;
function showButtons() {
  fileInput.disabled = playing || paused || uploading;
  document.getElementById("add-piece").setAttribute("aria-disabled", String(fileInput.disabled));
  playButton.disabled = !hasPiece || (!playing && (controllerBuilding || notesLoading || uploading || dynamicsChanging));
  playButton.innerHTML = icon(playing ? "pause" : "play");
  playButton.setAttribute("aria-label", playing ? "Pause" : paused ? "Resume" : "Play");
  stopButton.disabled = !playing && !paused;
  stateChip.textContent = playing ? "Playing" : paused ? "Paused" : hasPiece ? "Ready" : "No piece";
  stateChip.className = "chip" + (playing ? " accent" : paused ? " warn" : "");
}
noteView.onLoading((loading) => {
  notesLoading = loading;
  showBusy();
});

// The speed the loaded piece is built at, which its times (the bar, startFrom)
// are in, and its lead (times before it don't scale): from the status.
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
    const [pieceInfo, dynamics] = await Promise.all([ui.api("/api/piece"), ui.api("/api/dynamics")]);
    parts = pieceInfo.parts || [];
    offsets = dynamics.parts || [];
    muted = dynamics.muted || [];
  } catch {
    parts = [];
  }
  partInfo = parts;
  partOffsets = parts.map((_, i) => offsets[i] || 0);
  partMuted = parts.map((_, i) => Boolean(muted[i]));
  showParts();
  drawVelocities();
}

// One row per part, when the file has more than one, each with its offset.
function showParts() {
  const shown = partInfo.filter((p) => p.notes > 0).length > 1;
  partsSection.hidden = !shown;
  noteView.setPartCount(shown ? partInfo.length : 0);
  if (!shown) return;
  partsList.replaceChildren(...partInfo.map((part, i) => {
    const row = el("div", { class: "part" });
    const swatch = el("i", { class: "swatch", style: `border-color:${noteView.partColor(i)}` });
    const name = el("div", { class: "part-name" }, el("strong", {}, part.name),
      el("span", {}, part.notes
        ? `${part.notes} notes, ${noteName(part.low)}–${noteName(part.high)}, typically ${part.velocity}`
        : "no notes on the keys"));
    const slider = el("input", { type: "range", min: -20, max: 20, step: 1, value: partOffsets[i], "aria-label": part.name + " offset" });
    const value = el("output");
    const show = () => (value.textContent = (partOffsets[i] > 0 ? "+" : "") + partOffsets[i]);
    show();
    slider.addEventListener("input", () => {
      partOffsets[i] = Number(slider.value);
      show();
      dynamicsChanged();
    });
    // Mute: left out, as if the file didn't have it (never every part).
    const mute = el("button", { class: "btn btn-sm btn-icon mute", type: "button" });
    const showMute = () => {
      mute.innerHTML = icon(partMuted[i] ? "mute" : "volume");
      mute.setAttribute("aria-pressed", String(partMuted[i]));
      mute.setAttribute("aria-label", (partMuted[i] ? "Unmute " : "Mute ") + part.name);
      mute.title = partMuted[i] ? "Muted: click to bring it back" : "Mute: leave this track out";
      row.classList.toggle("muted", partMuted[i]);
      slider.disabled = partMuted[i];
    };
    showMute();
    mute.addEventListener("click", () => {
      const others = partInfo.filter((p, j) => p.notes > 0 && !partMuted[j] && j !== i).length;
      if (!partMuted[i] && others === 0) {
        ui.toast("Every other track is muted: unmute one first");
        return;
      }
      partMuted[i] = !partMuted[i];
      showMute();
      dynamicsChanged();
    });
    row.append(swatch, name, slider, value, mute);
    return row;
  }));
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

// The piece's notes as the file has them, and how many are struck with the
// settings now (merged repeats, doubled notes and muted tracks aren't): from
// the note view, which follows every change at once.
let noteCounts = null;
let pieceDuration = "";
function showMeta() {
  if (!hasPiece) return;
  const counts = noteCounts
    ? `${noteCounts.written.toLocaleString()} notes` +
      (noteCounts.played !== noteCounts.written ? ` · ${noteCounts.played.toLocaleString()} played` : "")
    : "";
  pieceMeta.textContent = [counts, pieceDuration].filter(Boolean).join(" · ");
  pieceMeta.title = noteCounts && noteCounts.played !== noteCounts.written
    ? "Played: struck with the settings now, without merged repeats, doubled notes and muted tracks" : "";
}

noteView.onLoaded((notes) => {
  const MERGED = 1, DUPLICATE = 32, MUTED = 128;
  noteCounts = notes ? { written: notes.length,
    played: notes.filter((n) => !(n.changes & (MERGED | DUPLICATE | MUTED))).length } : null;
  showMeta();
  showRepeats(notes);
  pieceNotes = notes ? shapedNotes(notes) : null;
  velocitySection.hidden = !pieceNotes;
  drawVelocities();
});

// ---- adding a piece ----
fileInput.addEventListener("change", async () => {
  const file = fileInput.files[0];
  if (!file) return;
  uploading = true;
  showBusy();
  closeLibrary();
  let summary = null;
  try {
    summary = await ui.api("/api/midi?name=" + encodeURIComponent(file.name), {
      method: "POST",
      headers: { "Content-Type": "application/octet-stream" },
      body: file,
    });
  } catch (error) {
    ui.toast(`Couldn't load ${file.name}: ${error.message}`, { error: true });
  }
  uploading = false;
  showBusy();
  loadLibrary();
  fileInput.value = "";
  if (!summary) return;
  const notices = [];
  const saved = {
    saved: "Saved in the library",
    already: `Already in the library as “${summary.title}”: loaded with its settings`,
    full: "The library is full, so it wasn't saved (delete some pieces to make room)",
    unavailable: "Not saved: the piano has no library space (upload the firmware with the new partition table)",
    failed: "Couldn't save it in the library",
  }[summary.library];
  if (saved) notices.push(saved);
  if (summary.skippedNotes > 0) notices.push(`${summary.skippedNotes} notes outside the 88 keys were skipped`);
  if (summary.mergedNotes > 0) notices.push(`${summary.mergedNotes} repeats too fast to strike again were held instead`);
  if (summary.duplicateNotes > 0) notices.push(`${summary.duplicateNotes} notes the file has twice at once are played once`);
  ui.toast(notices.join(". "), { error: summary.library === "full" || summary.library === "failed" });
  ui.refreshStatus();
});

// ---- playing ----
async function playOrPause() {
  if (playButton.disabled) return;
  if (playing) {
    await ui.request("/api/playback/pause", { method: "POST" });
  } else {
    // Paused, it goes on from where it stopped (or from where the bar was moved to).
    const from = !paused && startFrom > 0 ? `?from=${startFrom}&speed=${pieceSpeed}` : "";
    await ui.request("/api/playback/play" + from, { method: "POST" });
    loadLibrary(); // its last played
  }
  ui.refreshStatus();
}
playButton.addEventListener("click", playOrPause);
stopButton.addEventListener("click", async () => {
  await ui.request("/api/playback/stop", { method: "POST" });
  ui.refreshStatus();
});
// Space plays and pauses (not while typing).
document.addEventListener("keydown", (event) => {
  if (event.code === "Space" && !ui.typing(event.target) && !event.target.closest("button, a, [role=menu]")) {
    event.preventDefault();
    playOrPause();
  }
});

progress.addEventListener("input", () => {
  seeking = true;
  timeNow.textContent = formatTime(Number(progress.value));
});
progress.addEventListener("change", () => goTo(Number(progress.value)));

// Clicking the notes picks a point too.
noteView.onPick((ms) => goTo(Math.min(ms, durationMs)));

async function goTo(ms) {
  if (playing || paused) {
    await ui.request(`/api/playback/seek?to=${ms}&speed=${pieceSpeed}`, { method: "POST" });
  } else {
    startFrom = ms;
  }
  seeking = false;
  ui.refreshStatus();
}

// ---- the sound settings ----
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
    const saved = await ui.request("/api/dynamics", { json: settings() });
    dynamicsChanging = false;
    showButtons();
    if (saved) {
      noteView.load(); // merges and limits depend on the velocities
      volumeNote.textContent = playing ? "The new settings apply from the next Play, or after a pause." : "";
    }
  }, 300);
}

[speed, volume, contrast, voicing, repeatBoost].forEach((slider) => slider.addEventListener("input", dynamicsChanged));

resetButton.addEventListener("click", async () => {
  const sure = await ui.confirm({
    title: "Reset the sound settings?",
    message: "Speed, volume, contrast, voicing and repeats go back to their defaults, and every track to as written " +
      "(unmuted, no offset). This piece then remembers the defaults.",
    confirm: "Reset", danger: true,
  });
  if (!sure) return;
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

// ---- the library sheet (narrow screens) ----
const librarySheet = document.getElementById("library");
const libraryBackdrop = document.getElementById("library-backdrop");
function openLibrary() {
  librarySheet.classList.add("open");
  libraryBackdrop.hidden = false;
}
function closeLibrary() {
  librarySheet.classList.remove("open");
  libraryBackdrop.hidden = true;
}
document.getElementById("library-open").addEventListener("click", openLibrary);
document.getElementById("empty-library").addEventListener("click", openLibrary);
document.getElementById("library-close").addEventListener("click", closeLibrary);
libraryBackdrop.addEventListener("click", closeLibrary);
document.addEventListener("keydown", (event) => {
  if (event.key === "Escape" && librarySheet.classList.contains("open")) closeLibrary();
});

// ---- following the controller ----
function show(status, receivedAt) {
  const playback = status.playback;
  const heat = status.heat;
  heatLine.textContent = heat && heat.watched && heat.load < heat.resumeLoad ? ui.heatText(heat).line : "";
  controllerBuilding = Boolean(playback.building);
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
    noteCounts = null;
    if (pieceKey) {
      loadPiece();
      noteView.load(true); // a new piece: its notes from the start
    } else {
      notesSection.hidden = true;
      velocitySection.hidden = true;
      partsSection.hidden = true;
    }
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
  document.getElementById("player-empty").hidden = Boolean(playback.piece);
  document.getElementById("transport").hidden = !playback.piece;
  progress.closest(".scrubber").hidden = !playback.piece;
  if (playback.piece) {
    const saved = libraryPieces.find((p) => p.id === libraryId);
    piece.textContent = saved ? saved.name : playback.piece.title;
    document.title = `${piece.textContent} · Player piano`;
    pieceDuration = formatTime(playback.piece.durationMs) + (builtAt !== 100 ? ` at ${builtAt}%` : "");
    durationMs = playback.piece.durationMs;
    progress.max = Math.max(durationMs, 1);
    timeTotal.textContent = formatTime(durationMs);
    if (!seeking) {
      const position = playing || paused ? Math.min(playback.positionMs, durationMs) : startFrom;
      progress.value = position;
      timeNow.textContent = formatTime(position);
    }
  } else {
    piece.textContent = "No piece loaded";
    document.title = "Play · Player piano";
    pieceMeta.textContent = "Choose one from the library, or add a MIDI file.";
    durationMs = 0;
    progress.value = 0;
  }
  progress.disabled = !playback.piece;
  if (!seeking) {
    noteView.update(playing, playback.positionMs, paused ? playback.positionMs : startFrom, receivedAt);
  }
  hasPiece = Boolean(playback.piece);
  showMeta();
  showBusy();
}

ui.onStatus(show);
loadLibrary();
