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
  const SEND_AFTER_MS = 600; // after the last change, so a few clicks make one rebuild

  let editing = false;
  let edits = new Map(); // "key/source" -> [key, source ms, velocity, length ms, shift ms]
  let pedalEdits = new Map(); // source ms -> [source ms, down ms, up ms, removed 0/1]
  let saved = true; // kept with the piece in the library
  let current = null; // the note or pedal press ({pedal: true, ...}) being edited
  let changed = false; // edits not sent yet
  let sending = false;
  let sendTimer = 0;
  const idOf = (key, source) => `${key}/${source}`;
  const seconds = (ms) => `${(ms / 1000).toFixed(2)} s`;

  async function fetchEdits() {
    try {
      const reply = await ui.api("/api/piece/edits");
      if (changed || sending) return; // ours are newer
      useReply(reply);
    } catch { /* none loaded, or unreachable: keep what we had */ }
  }

  function useReply(reply) {
    edits = new Map(reply.notes.map((e) => [idOf(e[0], e[1]), e]));
    pedalEdits = new Map(reply.pedal.map((e) => [e[0], e]));
    saved = reply.saved;
  }

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
    const notes = [
      sending ? "Saving…" : changed ? "Not saved yet…" : what ? `${what} edited` : "Nothing edited yet",
      playingNow && (changed || sending || what) ? "changes apply from the next Play" : "",
      !saved ? "not kept: this piece isn't in the library" : "",
    ].filter(Boolean);
    status.replaceChildren(notes.join(" · "));
    if (what && !sending) {
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
    changed = true;
    clearTimeout(sendTimer);
    sendTimer = setTimeout(send, SEND_AFTER_MS);
    draw();
  }

  async function send() {
    clearTimeout(sendTimer);
    if (sending) {
      sendTimer = setTimeout(send, SEND_AFTER_MS); // after the one on its way
      return;
    }
    sending = true;
    changed = false;
    draw();
    const reply = await ui.request("/api/piece/edits", { json: { notes: [...edits.values()], pedal: [...pedalEdits.values()] } });
    sending = false;
    if (reply && !changed) useReply(reply);
    if (!reply) changed = true; // kept here: the next change tries again
    draw();
    if (reply) noteView.load(); // the notes and pedal as they'll now play
  }

  async function removeAll() {
    const sure = await ui.confirm({ title: "Remove every edit?",
      message: "The piece's notes and pedal all play as written again.", confirm: "Remove all", danger: true });
    if (!sure) return;
    edits.clear();
    pedalEdits.clear();
    changed = true;
    send();
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
    await fetchEdits();
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
