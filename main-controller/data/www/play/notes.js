// The piece's notes, as written and as they'll be played, like a MIDI editor:
// time runs left to right, keys go up. Notes the roll builder changed stand
// out, and stepping through them shows what happened to each. The view follows
// the playhead while playing; when stopped it can be dragged, and a click
// picks a point to play from (or, editing notes, picks a note: note-edit.js).
const noteView = (() => {
  const MERGED = 1, ABSORBED = 2, SHORTENED = 4, LENGTHENED = 8, LIMITED = 16, DUPLICATE = 32, BOOSTED = 64,
    MUTED = 128, EDITED = 256;
  // The kinds of change to step through: each a test of a note. A note
  // merged at the same moment as the strike it's in is doubled (two voices on
  // one key at different velocities), not a repeat too soon to strike again.
  const doubled = (n) => (n.changes & MERGED) && n.mergedInto === n.on;
  const KINDS = [
    { id: "merged", label: "merged repeats", test: (n) => (n.changes & MERGED) && !doubled(n) },
    { id: "doubled", label: "doubled", test: doubled },
    { id: "shortened", label: "shortened", test: (n) => n.changes & SHORTENED },
    { id: "lengthened", label: "lengthened", test: (n) => n.changes & LENGTHENED },
    { id: "limited", label: "velocity limited", test: (n) => n.changes & LIMITED },
    { id: "duplicates", label: "duplicates", test: (n) => n.changes & DUPLICATE },
    { id: "boosted", label: "repeats boosted", test: (n) => n.changes & BOOSTED },
    { id: "muted", label: "muted", test: (n) => n.changes & MUTED },
    { id: "edited", label: "edited by hand", test: (n) => n.changes & EDITED }
  ];
  const kindTest = () => (KINDS.find((k) => k.id === kindSelect.value) || KINDS[0]).test;
  // Velocity 1 to 127, light to dark, for played notes and changed ones.
  const RAMPS = {
    played: rampOf([[1, "#dce9fb"], [32, "#93bdf2"], [64, "#3d86dc"], [96, "#1b4f9c"], [127, "#0b2552"]]),
    changed: rampOf([[1, "#fde6d2"], [32, "#f6b27a"], [64, "#e07b28"], [96, "#a5500f"], [127, "#5e2a04"]])
  };
  function rampOf(stops) {
    const rgb = (hex) => [1, 3, 5].map((i) => parseInt(hex.slice(i, i + 2), 16));
    const colors = [];
    for (let v = 0; v <= 127; v++) {
      const upper = stops.findIndex(([at]) => at >= v);
      const [a, ca] = stops[Math.max(0, upper - 1)], [b, cb] = stops[Math.max(0, upper)];
      const t = b === a ? 0 : (v - a) / (b - a);
      const [r, g, bl] = rgb(ca).map((x, i) => Math.round(x + (rgb(cb)[i] - x) * t));
      colors.push(`rgb(${r},${g},${bl})`);
    }
    return colors;
  }
  // The theme's colours (app.css), read again whenever it changes.
  let COLORS = {};
  function readColors() {
    const t = ui.token;
    COLORS = {
      background: t("--chart-bg"), blackRow: t("--chart-row"), line: t("--chart-line"), text: t("--chart-text"),
      written: t("--chart-written"), played: t("--chart-played"), changed: t("--chart-changed"),
      merged: t("--chart-merged"), muted: t("--chart-muted"), playhead: t("--chart-playhead"),
      edited: t("--accent"),
    };
  }
  readColors();
  ui.onTheme(() => {
    readColors();
    redraw();
  });
  const GUTTER = 28;     // px for the C labels
  const PEDAL_ROW = 10;  // px per pedal band
  const AXIS = 16;       // px for the time labels
  const MIN_SPAN = 2000, MAX_SPAN = 60000;
  const NAMES = ["C", "C♯", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B"];

  const section = document.getElementById("notes");
  const canvas = document.getElementById("notes-canvas");
  const tip = document.getElementById("notes-tip");
  const kindSelect = document.getElementById("change-kind");
  const prevButton = document.getElementById("change-prev");
  const nextButton = document.getElementById("change-next");
  const changeText = document.getElementById("change-text");
  const ctx = canvas.getContext("2d");

  let data = null;       // from /api/piece/notes
  let notes = [];        // as /api/piece/notes gives them (see load)
  let lowKey = 0, highKey = 87;
  let span = 10000;      // ms across the view
  let viewStart = 0;     // ms on the playback timeline at the left edge
  let pieceEnd = 0;      // ms on the playback timeline when the last note or pedal ends
  let playing = false;
  // The playhead runs on the page's own clock, nudged toward each status
  // poll, so poll delays don't make it jump.
  let clockMs = 0, clockAt = 0;
  let marker = 0;        // where Play starts, when stopped
  let selected = null;   // the note stepped to
  let hovered = null;
  let pickHandler = () => {};
  let noteHandler = null; // editing: called with the note or pedal press clicked (or null)
  let describeEdit = () => ""; // a note's hand edit, in words (note-edit.js)
  let pedalEdited = () => false; // whether a pedal press (by its MIDI time) is edited
  let selectedPedal = null; // the pedal press being edited: {pedal: true, down, up, source}
  const loadedHandlers = [];
  let loadingHandler = () => {};
  let loads = 0; // the latest load(): an earlier one that finishes after it is dropped
  // With more than one part (track), each note's outline shows which it's from.
  const PART_COLORS = ["#7a4fd0", "#1f9a6f", "#d0456f", "#b07d00", "#2f7fbf", "#8a5a2b"];
  let partCount = 0;
  const partColor = (part) => PART_COLORS[part % PART_COLORS.length];
  let frame = 0;

  const lead = () => (data ? data.leadMs : 0);
  const keyName = (key) => NAMES[(key + 21) % 12] + (Math.floor((key + 21) / 12) - 1);
  const isBlack = (key) => [1, 3, 6, 8, 10].includes((key + 21) % 12);
  const formatTime = (ms) => {
    const s = Math.max(0, ms) / 1000;
    return Math.floor(s / 60) + ":" + (s % 60).toFixed(2).padStart(5, "0");
  };

  function layout() {
    const rows = highKey - lowKey + 1;
    // At most about half the screen's height (560 px on a large one).
    const budget = Math.max(220, Math.min(560, window.innerHeight * 0.5));
    const rowHeight = Math.max(4, Math.min(16, Math.floor(budget / rows)));
    const width = canvas.clientWidth || 600;
    const height = rows * rowHeight + 2 * PEDAL_ROW + 6 + AXIS;
    return { rows, rowHeight, width, height, plot: width - GUTTER };
  }

  const xOf = (ms, l) => GUTTER + ((ms - viewStart) / span) * l.plot;
  const timeOf = (x, l) => viewStart + ((x - GUTTER) / l.plot) * span;
  const yOf = (key, l) => (highKey - key) * l.rowHeight;

  function currentPosition() {
    return playing ? clockMs + (performance.now() - clockAt) : marker;
  }

  function draw() {
    frame = 0;
    if (!data) return;
    const l = layout();
    const ratio = window.devicePixelRatio || 1;
    if (canvas.width !== Math.round(l.width * ratio) || canvas.height !== Math.round(l.height * ratio)) {
      canvas.width = Math.round(l.width * ratio);
      canvas.height = Math.round(l.height * ratio);
      canvas.style.height = l.height + "px";
    }
    ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
    const now = currentPosition();
    if (playing) {
      viewStart = now - span * 0.25;
    } else {
      // Dragged, scrolled or zoomed: no further than a quarter of the view
      // before the start, or beyond the end at three quarters across.
      const first = -span * 0.25;
      viewStart = Math.max(first, Math.min(viewStart, Math.max(first, pieceEnd - span * 0.75)));
    }
    const shift = lead(); // notes are drawn where they sound: lead after the MIDI
    const keysHeight = l.rows * l.rowHeight;

    ctx.fillStyle = COLORS.background;
    ctx.fillRect(0, 0, l.width, l.height);
    ctx.font = "10px system-ui, sans-serif";
    ctx.textBaseline = "middle";
    for (let key = lowKey; key <= highKey; key++) {
      const y = yOf(key, l);
      if (isBlack(key)) {
        ctx.fillStyle = COLORS.blackRow;
        ctx.fillRect(GUTTER, y, l.plot, l.rowHeight);
      }
      if ((key + 21) % 12 === 0) {
        ctx.fillStyle = COLORS.line;
        ctx.fillRect(GUTTER, y + l.rowHeight - 0.5, l.plot, 1);
        ctx.fillStyle = COLORS.text;
        ctx.fillText(keyName(key), 2, y + l.rowHeight / 2);
      }
    }

    ctx.save();
    ctx.beginPath();
    ctx.rect(GUTTER, 0, l.plot, l.height);
    ctx.clip();

    const viewEnd = viewStart + span;
    for (const note of notes) {
      if (note.changes & DUPLICATE) continue; // drawn as the note it repeats
      const end = Math.max(note.writtenOff, note.playedOff) + shift;
      const start = (note.changes & (MERGED | MUTED) ? note.on : Math.min(note.on, note.playedOn)) + shift;
      if (start > viewEnd || end < viewStart) continue;
      const y = yOf(note.key, l);
      const x = xOf(note.on + shift, l);
      if (note.changes & MUTED) {
        // Left out: a faint dashed outline, as written.
        ctx.setLineDash([3, 3]);
        ctx.strokeStyle = COLORS.muted;
        ctx.lineWidth = 1;
        ctx.strokeRect(x + 0.5, y + 0.5, Math.max(xOf(note.writtenOff + shift, l) - x - 1, 1), l.rowHeight - 1);
        ctx.setLineDash([]);
        continue;
      }
      if (note.changes & MERGED) {
        ctx.globalAlpha = 0.25;
        ctx.fillStyle = COLORS.merged;
        ctx.fillRect(x, y + 0.5, Math.max(xOf(note.writtenOff + shift, l) - x, 2), l.rowHeight - 1);
        ctx.globalAlpha = 1;
        ctx.strokeStyle = COLORS.merged;
        ctx.lineWidth = 1.5;
        ctx.strokeRect(x, y + 0.75, Math.max(xOf(note.writtenOff + shift, l) - x, 2), l.rowHeight - 1.5);
        continue;
      }
      const changed = note.changes & (SHORTENED | LENGTHENED | ABSORBED | LIMITED | BOOSTED);
      const playedX = xOf(note.playedOn + shift, l);
      ctx.fillStyle = (changed ? RAMPS.changed : RAMPS.played)[note.playedVelocity];
      ctx.fillRect(playedX, y + 1, Math.max(xOf(note.playedOff + shift, l) - playedX, 2), l.rowHeight - 2);
      ctx.strokeStyle = partCount > 1 ? partColor(note.part) : COLORS.written;
      ctx.lineWidth = partCount > 1 ? 1.5 : 1;
      ctx.strokeRect(x + 0.5, y + 0.5, Math.max(xOf(note.writtenOff + shift, l) - x - 1, 1), l.rowHeight - 1);
      if (note.changes & EDITED) {
        // Edited by hand: a mark where it's played from.
        ctx.fillStyle = COLORS.edited;
        ctx.fillRect(playedX - 1, y - 1, 3, l.rowHeight + 2);
      }
    }
    if (selected) {
      const y = yOf(selected.key, l);
      const x = xOf(selected.on + shift, l);
      const off = Math.max(selected.writtenOff, selected.playedOff) + shift;
      ctx.strokeStyle = COLORS.playhead;
      ctx.lineWidth = 2;
      ctx.strokeRect(x - 3, y - 3, Math.max(xOf(off, l) - x, 2) + 6, l.rowHeight + 6);
    }

    // The pedal: as written (outline) above as played (filled).
    const pedalTop = keysHeight + 3;
    const band = (spans, row, filled) => {
      for (const [down, up] of spans) {
        const x1 = xOf(down + shift, l), x2 = xOf(up + shift, l);
        if (x2 < GUTTER || x1 > l.width) continue;
        const y = pedalTop + row * PEDAL_ROW;
        if (filled) {
          ctx.globalAlpha = 0.45;
          ctx.fillStyle = COLORS.played;
          ctx.fillRect(x1, y + 1, Math.max(x2 - x1, 1), PEDAL_ROW - 2);
          ctx.globalAlpha = 1;
        } else {
          ctx.strokeStyle = COLORS.written;
          ctx.lineWidth = 1;
          ctx.strokeRect(x1 + 0.5, y + 1.5, Math.max(x2 - x1 - 1, 1), PEDAL_ROW - 3);
        }
      }
    };
    band(data.writtenPedal, 0, false);
    band(data.playedPedal, 1, true);
    // Pedal presses edited by hand, and the one being edited.
    for (const [down, up, source] of data.writtenPedal) {
      const isSelected = selectedPedal && selectedPedal.source === source;
      if (!isSelected && !pedalEdited(source)) continue;
      const x1 = xOf(down + shift, l), x2 = xOf(up + shift, l);
      if (x2 < GUTTER || x1 > l.width) continue;
      ctx.strokeStyle = isSelected ? COLORS.playhead : COLORS.edited;
      ctx.lineWidth = isSelected ? 2 : 1.5;
      ctx.strokeRect(x1 - (isSelected ? 2 : 0), pedalTop - (isSelected ? 1 : 0), Math.max(x2 - x1, 2) + (isSelected ? 4 : 0),
        2 * PEDAL_ROW + (isSelected ? 2 : 0));
    }

    // Seconds along the bottom.
    const axisY = pedalTop + 2 * PEDAL_ROW + 3;
    const step = [1000, 2000, 5000, 10000].find((s) => (s / span) * l.plot >= 60) || 10000;
    ctx.fillStyle = COLORS.text;
    ctx.textBaseline = "top";
    for (let t = Math.ceil(viewStart / step) * step; t <= viewEnd; t += step) {
      if (t < 0) continue;
      const x = xOf(t, l);
      ctx.fillStyle = COLORS.line;
      ctx.fillRect(x, axisY - 3, 1, 3);
      ctx.fillStyle = COLORS.text;
      ctx.fillText(formatTime(t).replace(/\.\d+$/, ""), x + 2, axisY);
    }

    // The playhead, or where Play will start.
    const px = xOf(now, l);
    ctx.fillStyle = COLORS.playhead;
    ctx.globalAlpha = playing ? 1 : 0.5;
    ctx.fillRect(px - 0.75, 0, 1.5, axisY - 3);
    ctx.globalAlpha = 1;
    ctx.restore();

    ctx.fillStyle = COLORS.text;
    ctx.textBaseline = "middle";
    ctx.fillText("ped", 2, pedalTop + PEDAL_ROW);

    if (playing) {
      frame = requestAnimationFrame(draw);
    }
  }

  function redraw() {
    if (!frame) frame = requestAnimationFrame(draw);
  }

  // What happened to a note, in words.
  function describe(note) {
    const lines = [];
    const velocity = note.changes & (MERGED | DUPLICATE | MUTED) ? note.writtenVelocity
      : note.writtenVelocity === note.playedVelocity ? note.playedVelocity
      : note.writtenVelocity + " → " + note.playedVelocity;
    const head = `${keyName(note.key)} at ${formatTime(note.on + lead())}, velocity ${velocity}`;
    if (note.changes & MERGED) {
      const strike = notes.find((n) => n.key === note.key && n.on === note.mergedInto && !(n.changes & (MERGED | DUPLICATE)));
      const gap = note.on - note.mergedInto;
      if (gap === 0) {
        lines.push("Doubled: the file has another note on this key at the same time (two voices sharing it, or a " +
          "duplicate in the file), so the key is struck once.");
      } else if (gap > 0) {
        lines.push(`Merged: repeats ${gap} ms after the strike before, too soon for the key to strike again, so it's held on in that strike.`);
      } else {
        lines.push(`Merged: written ${-gap} ms before another note on this key${strike ? ` (velocity ${strike.writtenVelocity})` : ""}, ` +
          "too close for two strikes, so they're struck once, at the louder velocity, when the first should sound.");
      }
    }
    if (note.changes & MUTED) {
      lines.push("Muted: its track is left out, as if the file didn't have it.");
    }
    const edit = describeEdit(note);
    if (edit) lines.push(`Edited by hand: ${edit}.`);
    if (note.changes & BOOSTED) {
      lines.push(`Repeat played louder (the Repeats setting), at velocity ${note.playedVelocity}, so the key can ` +
        "strike again this soon.");
    }
    if (note.changes & DUPLICATE) {
      lines.push("Duplicate: the file has this note twice at once, at the same velocity, so it's played once.");
    }
    if (note.changes & ABSORBED) {
      const merged = notes.filter((n) => n.key === note.key && (n.changes & MERGED) && n.mergedInto === note.on).length;
      const parts = [`Holds ${merged} merged ${merged === 1 ? "note" : "notes"}`];
      if (note.playedVelocity > note.writtenVelocity) parts.push(`struck at the louder velocity, ${note.playedVelocity}`);
      if (note.playedOn !== note.on) parts.push(`sounding at ${formatTime(note.playedOn + lead())}, with the first of them`);
      if (note.playedOff > note.writtenOff) parts.push(`held ${note.playedOff - note.writtenOff} ms longer`);
      lines.push(parts.join(", ") + ".");
    }
    if (note.changes & SHORTENED) {
      lines.push(`Shortened: released ${note.writtenOff - note.playedOff} ms early so the key can reset before its next strike.`);
    }
    if (note.changes & LENGTHENED) {
      lines.push(`Lengthened: held ${note.playedOff - note.writtenOff} ms longer, until its hammer has landed.`);
    }
    if (note.changes & LIMITED) {
      lines.push(`Velocity limited: the volume took it past ${note.playedVelocity === 127 ? "127" : "1"}.`);
    }
    return { head, lines };
  }

  function noteAt(x, y) {
    const l = layout();
    if (x < GUTTER || y >= l.rows * l.rowHeight) return null;
    const key = highKey - Math.floor(y / l.rowHeight);
    const t = timeOf(x, l) - lead();
    const slack = (3 / l.plot) * span; // a few px either side, for short notes
    let found = null;
    for (const note of notes) {
      if (note.key !== key || note.changes & DUPLICATE) continue;
      const end = Math.max(note.writtenOff, note.playedOff);
      if (t >= note.on - slack && t <= end + slack) found = note;
    }
    return found;
  }

  // The pedal press (as written) at a point in the pedal rows, or null: in
  // the played row, the written press nearest the one played there.
  function pedalAt(x, y) {
    const l = layout();
    const top = l.rows * l.rowHeight + 3;
    if (!data || x < GUTTER || y < top || y > top + 2 * PEDAL_ROW) return null;
    const t = timeOf(x, l) - lead();
    const slack = (3 / l.plot) * span;
    const toPress = ([down, up, source]) => ({ pedal: true, down, up, source });
    const written = data.writtenPedal.find(([down, up]) => t >= down - slack && t <= up + slack);
    if (written || y < top + PEDAL_ROW) return written ? toPress(written) : null;
    const played = data.playedPedal.find(([down, up]) => t >= down - slack && t <= up + slack);
    if (!played || !data.writtenPedal.length) return null;
    const nearest = data.writtenPedal.reduce((best, s) => (Math.abs(s[0] - played[0]) < Math.abs(best[0] - played[0]) ? s : best));
    return toPress(nearest);
  }

  // ---- stepping through changes ----
  function kindNotes() {
    return notes.filter(kindTest());
  }

  function showSelected() {
    if (!selected || noteHandler) { // editing: the editor says what it is
      changeText.textContent = "";
      return;
    }
    const list = kindNotes();
    const { head, lines } = describe(selected);
    changeText.textContent = `${list.indexOf(selected) + 1} of ${list.length}: ${head}. ${lines.join(" ")}`;
  }

  function step(direction) {
    const list = kindNotes();
    if (!list.length) return;
    let index = selected ? list.indexOf(selected) : -1;
    if (index < 0) {
      // From the middle of the view.
      const middle = viewStart + span / 2 - lead();
      index = direction > 0 ? list.findIndex((n) => n.on > middle) : list.findLastIndex((n) => n.on < middle);
      if (index < 0) index = direction > 0 ? 0 : list.length - 1;
    } else {
      index = (index + direction + list.length) % list.length;
    }
    selected = list[index];
    if (!playing) {
      viewStart = selected.on + lead() - span * 0.4;
    }
    if (noteHandler) noteHandler(selected); // editing: on to that note
    showSelected();
    redraw();
  }

  function fillKinds() {
    const chosen = kindSelect.value;
    kindSelect.replaceChildren(...KINDS.map((kind) => {
      const option = document.createElement("option");
      option.value = kind.id;
      option.textContent = `${kind.label} (${notes.filter(kind.test).length})`;
      return option;
    }));
    if (chosen) kindSelect.value = chosen;
  }

  kindSelect.addEventListener("change", () => { selected = null; showSelected(); redraw(); });
  prevButton.addEventListener("click", () => step(-1));
  nextButton.addEventListener("click", () => step(1));

  // ---- dragging, clicking, zooming (pinching), hovering ----
  let drag = null;
  const touches = new Map(); // pointerId -> x, while down
  let pinch = null; // {distance, span, at}: two fingers down
  canvas.addEventListener("pointerdown", (event) => {
    touches.set(event.pointerId, event.clientX);
    canvas.setPointerCapture(event.pointerId);
    if (touches.size === 2) {
      const [a, b] = [...touches.values()];
      const rect = canvas.getBoundingClientRect();
      pinch = { distance: Math.max(Math.abs(a - b), 20), span, at: timeOf((a + b) / 2 - rect.left, layout()) };
      drag = null;
      return;
    }
    drag = { x: event.clientX, viewStart, moved: false };
  });
  const lift = (event) => {
    touches.delete(event.pointerId);
    if (touches.size < 2) pinch = null;
  };
  canvas.addEventListener("pointercancel", (event) => {
    lift(event);
    drag = null;
  });
  canvas.addEventListener("pointermove", (event) => {
    const rect = canvas.getBoundingClientRect();
    if (touches.has(event.pointerId)) touches.set(event.pointerId, event.clientX);
    if (pinch && touches.size === 2) {
      const [a, b] = [...touches.values()];
      const l = layout();
      span = Math.min(MAX_SPAN, Math.max(MIN_SPAN, (pinch.span * pinch.distance) / Math.max(Math.abs(a - b), 20)));
      const middle = (a + b) / 2 - rect.left;
      if (!playing) viewStart = pinch.at - ((middle - GUTTER) / l.plot) * span;
      redraw();
      return;
    }
    if (drag) {
      const dx = event.clientX - drag.x;
      if (Math.abs(dx) > 4) drag.moved = true;
      if (drag.moved && !playing) {
        viewStart = drag.viewStart - (dx / layout().plot) * span;
        redraw();
      }
      return;
    }
    const note = noteAt(event.clientX - rect.left, event.clientY - rect.top);
    if (note !== hovered) {
      hovered = note;
      if (note) {
        const { head, lines } = describe(note);
        tip.textContent = [head, ...lines].join("\n");
      }
      tip.hidden = !note;
    }
    if (note) {
      const x = event.clientX - rect.left;
      tip.style.left = Math.min(x + 12, canvas.clientWidth - tip.offsetWidth) + "px";
      tip.style.top = (event.clientY - rect.top + 14) + "px";
    }
  });
  canvas.addEventListener("pointerleave", () => { hovered = null; tip.hidden = true; });
  canvas.addEventListener("pointerup", (event) => {
    lift(event);
    if (drag && !drag.moved) {
      const rect = canvas.getBoundingClientRect();
      const x = event.clientX - rect.left;
      if (noteHandler) {
        const y = event.clientY - rect.top;
        noteHandler(noteAt(x, y) || pedalAt(x, y));
      } else if (x >= GUTTER) {
        pickHandler(Math.max(0, Math.round(timeOf(x, layout()))));
      }
    }
    drag = null;
  });
  canvas.addEventListener("wheel", (event) => {
    const l = layout();
    if (event.ctrlKey || event.metaKey) {
      event.preventDefault();
      const rect = canvas.getBoundingClientRect();
      const at = timeOf(event.clientX - rect.left, l);
      const next = Math.min(MAX_SPAN, Math.max(MIN_SPAN, span * Math.exp(event.deltaY * 0.01)));
      viewStart = at - ((at - viewStart) * next) / span;
      span = next;
      redraw();
    } else if (Math.abs(event.deltaX) > Math.abs(event.deltaY) || event.shiftKey) {
      event.preventDefault();
      if (!playing) {
        viewStart += ((event.shiftKey ? event.deltaY : event.deltaX) / l.plot) * span;
        redraw();
      }
    }
  }, { passive: false });
  document.getElementById("zoom-in").addEventListener("click", () => zoom(0.5));
  document.getElementById("zoom-out").addEventListener("click", () => zoom(2));
  function zoom(factor) {
    const middle = playing ? currentPosition() : viewStart + span / 2;
    span = Math.min(MAX_SPAN, Math.max(MIN_SPAN, span * factor));
    viewStart = middle - span / 2;
    redraw();
  }
  window.addEventListener("resize", redraw);

  return {
    // Loads the loaded piece's notes with the current dynamics; hides the view
    // without one. fromStart: a new piece, so the view goes back to its start.
    async load(fromStart = false) {
      if (fromStart) {
        marker = 0;
        viewStart = -span * 0.25;
        selected = null;
      }
      const before = data;
      const load = ++loads;
      loadingHandler(true);
      let fetched;
      try {
        const response = await fetch("/api/piece/notes");
        fetched = response.ok ? await response.json() : null;
      } catch (error) {
        fetched = null;
      }
      if (load !== loads) return; // a newer load is under way
      loadingHandler(false);
      data = fetched;
      // At a new speed, the view stays on the same point in the music.
      if (!fromStart && before && data && before.speed && data.speed && before.speed !== data.speed) {
        const moved = (ms) => (ms > data.leadMs ? data.leadMs + ((ms - data.leadMs) * before.speed) / data.speed : ms);
        const middle = moved(viewStart + span / 2);
        viewStart = middle - span / 2;
        marker = moved(marker);
      }
      section.hidden = !data;
      if (!data) {
        loadedHandlers.forEach((handler) => handler(null));
        return;
      }
      notes = data.notes.map(([key, on, writtenOff, playedOn, playedOff, writtenVelocity, playedVelocity, changes,
        mergedInto, part, source]) => ({ key, on, writtenOff, playedOn, playedOff, writtenVelocity, playedVelocity,
        changes, mergedInto, part: part || 0, source: source ?? on }));
      pieceEnd = data.leadMs + Math.max(0, ...notes.map((n) => Math.max(n.writtenOff, n.playedOff)),
        ...data.writtenPedal.map((s) => s[1]), ...data.playedPedal.map((s) => s[1]));
      const keys = notes.map((n) => n.key);
      lowKey = Math.max(0, Math.min(...keys) - 1);
      highKey = Math.min(87, Math.max(...keys) + 1);
      const wasSelected = selected;
      selected = wasSelected ? notes.find((n) => n.key === wasSelected.key && n.on === wasSelected.on) || null : null;
      if (selected && !noteHandler && !kindTest()(selected)) selected = null;
      fillKinds();
      showSelected();
      redraw();
      loadedHandlers.forEach((handler) => handler(notes));
    },
    // From each status poll: the playback state, and the position on the
    // playback timeline at sampledAt (performance.now(), half way through the
    // request). A jump (play, seek, resume) moves the clock at once; small
    // differences, from how long replies take, are eased out.
    update(isPlaying, positionMs, startFrom, sampledAt) {
      const started = isPlaying && !playing;
      const stopped = !isPlaying && playing; // finished, stopped or paused
      playing = isPlaying;
      const now = performance.now();
      const reported = positionMs + (now - sampledAt);
      const error = reported - (clockMs + (now - clockAt));
      clockMs = started || Math.abs(error) > 250 ? reported : clockMs + (now - clockAt) + error * 0.15;
      clockAt = now;
      if (!playing && (stopped || startFrom !== marker)) {
        // Back to where Play starts (the start when a piece finishes or is stopped, the pause point when paused).
        marker = startFrom;
        if (stopped || marker < viewStart || marker > viewStart + span) viewStart = marker - span * 0.25;
      }
      if (started || !playing) redraw();
    },
    // Called with a time on the playback timeline when the view is clicked.
    onPick(handler) {
      pickHandler = handler;
    },
    // Called with every note (as load gives them) each time they're loaded, or null.
    // handler(true) when it starts building the notes, (false) when they're in.
    onLoading(handler) {
      loadingHandler = handler;
    },
    onLoaded(handler) {
      loadedHandlers.push(handler);
    },
    // Editing notes: clicks pick a note (handler(note), or null for none)
    // instead of where to play from; null goes back to that.
    onNoteClick(handler) {
      noteHandler = handler;
      canvas.classList.toggle("editing", Boolean(handler));
    },
    // Marks a note (null: none), as stepping through changes does.
    select(target) {
      selected = target && !target.pedal ? target : null;
      selectedPedal = target && target.pedal ? target : null;
      showSelected();
      redraw();
    },
    // The pedal press now loaded that goes down at this MIDI time, or null.
    findPedal(source) {
      const span = data && data.writtenPedal.find((s) => s[2] === source);
      return span ? { pedal: true, down: span[0], up: span[1], source } : null;
    },
    pedalEditedWith(fn) {
      pedalEdited = fn;
      redraw();
    },
    // The note now loaded for a key and MIDI time, or null.
    findNote(key, source) {
      return notes.find((n) => n.key === key && n.source === source && !(n.changes & DUPLICATE)) || null;
    },
    describeEditWith(fn) {
      describeEdit = fn;
    },
    keyName,
    // A note's time as the view shows it (on the playback timeline).
    timeText: (ms) => formatTime(ms + lead()),
    // How many parts the piece has: with more than one, notes show which they're from.
    setPartCount(count) {
      partCount = count;
      redraw();
    },
    partColor,
  };
})();
