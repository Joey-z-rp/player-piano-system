// Editing the piece in the note view: with "Edit notes" on, a click picks a
// note, to play it louder or softer, longer or shorter, or earlier or later,
// or a sustain pedal press (in the pedal rows), to put it down or lift it
// earlier or later, or leave it out. Edits are kept with the piece (the
// controller's /api/piece/edits) and apply from the next Play. A note is
// named by its key and its note-on in the MIDI, a pedal press by when it goes
// down (note_edits.h), so edits stay with them at any speed. Uses noteView
// (notes.js) and ui (/assets/app.js).
(() => {
  const $ = (id) => document.getElementById(id);
  const toggle = $("edit-notes");
  const panel = $("note-edit");
  const head = $("note-edit-head");
  const noteFields = $("note-edit-fields");
  const pedalFields = $("pedal-edit-fields");
  const status = $("note-edit-status");
  const NOTE_FIELDS = [
    { input: $("edit-velocity"), max: 126, step: 1 },
    { input: $("edit-length"), max: 5000, step: 10 },
    { input: $("edit-shift"), max: 1000, step: 5 },
  ];
  const PEDAL_FIELDS = [
    { input: $("edit-pedal-down"), max: 2000, step: 10 },
    { input: $("edit-pedal-up"), max: 2000, step: 10 },
  ];
  const removedBox = $("edit-pedal-removed");
  const SAVE_AFTER_MS = 300;   // a run of clicks is saved once
  const BUILD_AFTER_MS = 2000; // the notes are built again once editing pauses this long
  const RETRY_AFTER_MS = 3000;

  let editing = false;
  // The edits as set here, as the controller has them (last read or saved),
  // and as the notes on show were built with: each {notes: Map "key/source"
  // -> [key, source ms, velocity, length ms, shift ms], pedal: Map source ms
  // -> [source ms, down ms, up ms, removed 0/1]}.
  let edits = new Map(), pedalEdits = new Map();
  const none = () => ({ notes: new Map(), pedal: new Map() });
  const copy = () => ({ notes: new Map(edits), pedal: new Map(pedalEdits) });
  let server = none(), shown = none();
  let saved = true; // kept with the piece in the library
  let current = null; // the note or pedal press ({pedal: true, ...}) being edited
  let version = 0, savedVersion = 0; // changes made here, and saved
  let saving = false, saveFailed = false, building = false, buildWanted = false;
  let saveTimer = 0, buildTimer = 0;
  let loadingWith = null, loadingWhileSaving = false, freshPiece = true;
  const idOf = (key, source) => `${key}/${source}`;
  const seconds = (ms) => `${(ms / 1000).toFixed(2)} s`;

  // The controller's edits. Unless force (another piece), not over changes
  // made here that it doesn't have yet.
  async function fetchEdits(force = false) {
    let reply;
    try {
      reply = await ui.api("/api/piece/edits");
    } catch { return; /* none loaded, or unreachable: keep what we had */ }
    if (!force && (saving || version !== savedVersion)) return;
    edits = new Map(reply.notes.map((e) => [idOf(e[0], e[1]), e]));
    pedalEdits = new Map(reply.pedal.map((e) => [e[0], e]));
    saved = reply.saved;
    server = copy();
    savedVersion = version;
  }

  // ---- shown before they're built ----
  const sameEdit = (a, b) => JSON.stringify(a || null) === JSON.stringify(b || null);
  // Each edit not in the notes on show yet, drawn as set: velocity (at the
  // volume) and times (at the speed) moved by the difference. The
  // controller's rules (repeats, the pedal) apply when they're built.
  function applyPreviews() {
    const built = noteView.builtWith();
    if (!built) return;
    const atSpeed = 100 / built.speed, atVolume = (built.volume ?? 100) / 100;
    const ids = new Set([...edits.keys(), ...shown.notes.keys()]);
    for (const id of ids) {
      const set = edits.get(id), was = shown.notes.get(id);
      if (sameEdit(set, was)) continue;
      const [key, source] = (set || was).slice(0, 2);
      noteView.previewNote(key, source, (note) => {
        const from = note.previewOf || was || [key, source, 0, 0, 0];
        const to = set || [key, source, 0, 0, 0];
        note.previewOf = to;
        if (!note.playedVelocity) return; // merged or muted: as it'll play once built
        note.playedVelocity = Math.min(127, Math.max(1, Math.round(note.playedVelocity + (to[2] - from[2]) * atVolume)));
        const shift = (to[4] - from[4]) * atSpeed, length = (to[3] - from[3]) * atSpeed;
        note.playedOn = Math.max(0, note.playedOn + shift);
        note.playedOff = Math.max(note.playedOn + 1, note.playedOff + shift + length);
      });
    }
    const presses = [];
    for (const source of new Set([...pedalEdits.keys(), ...shown.pedal.keys()])) {
      const set = pedalEdits.get(source), was = shown.pedal.get(source);
      if (sameEdit(set, was)) continue;
      const press = noteView.findPedal(source);
      if (!press) continue;
      const [, down, up, removed] = set || [source, 0, 0, 0];
      presses.push({ down: press.down + down * atSpeed, up: press.up + up * atSpeed, removed: Boolean(removed) });
    }
    noteView.setPedalPreview(presses);
  }

  // ---- saving, then building ----
  function scheduleSave(ms = SAVE_AFTER_MS) {
    clearTimeout(saveTimer);
    saveTimer = setTimeout(save, ms);
  }
  function scheduleBuild(ms = BUILD_AFTER_MS) {
    buildWanted = true;
    clearTimeout(buildTimer);
    buildTimer = setTimeout(build, ms);
  }

  async function save() {
    clearTimeout(saveTimer);
    if (saving) {
      scheduleSave(); // after the one on its way
      return;
    }
    const sending = version, snapshot = copy();
    saving = true;
    draw();
    const reply = await ui.request("/api/piece/edits",
      { json: { notes: [...snapshot.notes.values()], pedal: [...snapshot.pedal.values()] } });
    saving = false;
    saveFailed = !reply;
    if (reply) {
      savedVersion = sending;
      server = snapshot;
      saved = reply.saved;
    }
    if (version !== savedVersion) scheduleSave(reply ? SAVE_AFTER_MS : RETRY_AFTER_MS);
    draw();
  }

  // The notes as they'll now play: once everything's saved, without the
  // busy overlay (editing can go on meanwhile).
  async function build() {
    clearTimeout(buildTimer);
    if (saving || version !== savedVersion) {
      buildTimer = setTimeout(build, SAVE_AFTER_MS);
      return;
    }
    buildWanted = false;
    building = true;
    draw();
    await noteView.load(false, { quiet: true });
    building = false;
    draw();
  }

  // Each load: what it's built with is what the controller has as it starts.
  noteView.onLoadStart((fromStart) => {
    if (fromStart) freshPiece = true;
    loadingWith = { notes: new Map(server.notes), pedal: new Map(server.pedal) };
    loadingWhileSaving = saving;
  });

  // A note's edit, in words ("" for none).
  function describe(note) {
    const e = note && edits.get(idOf(note.key, note.source));
    if (!e) return "";
    const [, , velocity, length, shift] = e;
    return [velocity ? `velocity ${velocity > 0 ? "+" : ""}${velocity}` : "",
      length ? `${Math.abs(length)} ms ${length > 0 ? "longer" : "shorter"}` : "",
      shift ? `${Math.abs(shift)} ms ${shift < 0 ? "earlier" : "later"}` : ""].filter(Boolean).join(", ");
  }
  noteView.describeEditWith(describe);
  noteView.pedalEditedWith((source) => pedalEdits.has(source));

  function drawNote() {
    const e = edits.get(idOf(current.key, current.source)) || [current.key, current.source, 0, 0, 0];
    const velocity = current.playedVelocity && current.playedVelocity !== current.writtenVelocity
      ? `${current.writtenVelocity}, played at ${current.playedVelocity}` : `${current.writtenVelocity}`;
    head.replaceChildren(ui.el("strong", {}, `${noteView.keyName(current.key)} at ${noteView.timeText(current.on)}`),
      `, velocity ${velocity}` + (current.changes & 128 ? " (its track is muted: the edit applies when it's played)" : ""));
    NOTE_FIELDS.forEach((f, i) => {
      if (document.activeElement !== f.input) f.input.value = e[i + 2];
    });
  }

  function drawPedal() {
    const e = pedalEdits.get(current.source) || [current.source, 0, 0, 0];
    head.replaceChildren(ui.el("strong", {}, `Pedal down at ${noteView.timeText(current.down)}`),
      `, lifted ${seconds(current.up - current.down)} later, as written` + (e[3] ? " (left out)" : ""));
    PEDAL_FIELDS.forEach((f, i) => {
      if (document.activeElement !== f.input) f.input.value = e[i + 1];
      f.input.disabled = Boolean(e[3]);
      f.input.parentElement.querySelectorAll("button").forEach((b) => (b.disabled = Boolean(e[3])));
    });
    removedBox.checked = Boolean(e[3]);
  }

  function draw() {
    panel.hidden = !editing;
    toggle.setAttribute("aria-pressed", String(editing));
    toggle.classList.toggle("btn-primary", editing);
    if (!editing) return;
    const playingNow = ui.status?.playback?.state === "playing";
    noteFields.hidden = !current || current.pedal;
    pedalFields.hidden = !current || !current.pedal;
    if (!current) {
      head.textContent = "Click a note, or a pedal press in the pedal rows, to edit it. The arrows beside “Changes” step " +
        "through the notes (choose “edited by hand” for the ones you've edited).";
    } else if (current.pedal) {
      drawPedal();
    } else {
      drawNote();
    }
    const count = (n, one, many) => `${n} ${n === 1 ? one : many}`;
    const what = [edits.size ? count(edits.size, "note", "notes") : "", pedalEdits.size ? count(pedalEdits.size, "pedal press", "pedal presses") : ""]
      .filter(Boolean).join(" and ");
    const unsaved = version !== savedVersion;
    const notes = [
      what ? `${what} edited` : "Nothing edited yet",
      saveFailed ? "couldn't save: trying again" : saving || unsaved ? "saving…" : "",
      building ? "updating the notes…" : buildWanted ? "dashed: as you set them, until the notes update when you pause" : "",
      playingNow && (unsaved || what) ? "changes apply from the next Play" : "",
      !saved ? "not kept: this piece isn't in the library" : "",
    ].filter(Boolean);
    status.replaceChildren(notes.join(" · "));
    if (what) {
      status.append(" ", ui.el("button", { class: "btn btn-ghost btn-sm danger-text", type: "button", onclick: removeAll },
        "Remove all"));
    }
  }

  function pick(target) {
    current = target;
    noteView.select(target);
    draw();
  }

  const fieldValues = (list) => list.map((f) => Math.max(-f.max, Math.min(f.max, Math.round(Number(f.input.value) || 0))));

  // The current note's or pedal press's edit from the fields.
  function readFields() {
    if (!current) return;
    if (current.pedal) {
      const [down, up] = fieldValues(PEDAL_FIELDS);
      const removed = removedBox.checked ? 1 : 0;
      if (!down && !up && !removed) pedalEdits.delete(current.source);
      else pedalEdits.set(current.source, [current.source, down, up, removed]);
    } else {
      const values = fieldValues(NOTE_FIELDS);
      const id = idOf(current.key, current.source);
      if (values.every((v) => v === 0)) edits.delete(id);
      else edits.set(id, [current.key, current.source, ...values]);
    }
    changed();
  }

  // After any change here: shown at once, saved soon, built once editing pauses.
  function changed() {
    version++;
    applyPreviews();
    scheduleSave();
    scheduleBuild();
    draw();
  }

  async function removeAll() {
    const sure = await ui.confirm({ title: "Remove every edit?",
      message: "The piece's notes and pedal all play as written again.", confirm: "Remove all", danger: true });
    if (!sure) return;
    edits.clear();
    pedalEdits.clear();
    changed();
  }

  [...NOTE_FIELDS, ...PEDAL_FIELDS].forEach((f) => {
    f.input.addEventListener("change", readFields);
    f.input.addEventListener("keydown", (event) => {
      if (event.key === "Enter") readFields();
    });
    f.input.parentElement.querySelectorAll("[data-step]").forEach((button) => button.addEventListener("click", () => {
      f.input.value = (Math.round(Number(f.input.value) || 0)) + Number(button.dataset.step) * f.step;
      readFields();
    }));
  });
  removedBox.addEventListener("change", readFields);
  document.querySelectorAll(".edit-reset").forEach((button) => button.addEventListener("click", () => {
    [...NOTE_FIELDS, ...PEDAL_FIELDS].forEach((f) => (f.input.value = 0));
    removedBox.checked = false;
    readFields();
  }));

  toggle.addEventListener("click", () => {
    editing = !editing;
    noteView.onNoteClick(editing ? pick : null);
    if (!editing) {
      current = null;
      noteView.select(null);
      if (buildWanted) build(); // done editing: the notes as they'll play, now
    } else {
      fetchEdits().then(draw);
    }
    draw();
  });
  document.addEventListener("keydown", (event) => {
    if (editing && event.key === "Escape" && current && !event.target.closest("input")) pick(null);
  });

  // Each time the notes come in: the edits (another piece, or changed
  // elsewhere), and what's being edited as it now plays.
  noteView.onLoaded(async (notes) => {
    if (!notes) {
      current = null;
      draw();
      return;
    }
    if (freshPiece) {
      // Another piece (or the first): its own edits, and nothing pending.
      freshPiece = false;
      clearTimeout(saveTimer);
      clearTimeout(buildTimer);
      buildWanted = false;
      await fetchEdits(true);
      shown = copy();
    } else {
      shown = loadingWith || server;
      // Saved during the load: it may not have them, so again in a moment.
      if (loadingWhileSaving) scheduleBuild(SAVE_AFTER_MS);
    }
    applyPreviews();
    if (current) {
      current = current.pedal ? noteView.findPedal(current.source) : noteView.findNote(current.key, current.source);
      noteView.select(current);
    }
    draw();
  });
  let wasPlaying = false;
  ui.onStatus((s) => {
    const playingNow = s.playback?.state === "playing";
    if (playingNow !== wasPlaying && editing) draw(); // "applies from the next Play" comes and goes
    wasPlaying = playingNow;
  });
})();
