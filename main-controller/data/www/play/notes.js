// The piece's notes, as written and as they'll be played, like a MIDI editor:
// time runs left to right, keys go up. Notes the roll builder changed stand
// out, and stepping through them shows what happened to each. The view follows
// the playhead while playing; when stopped it can be dragged, and a click
// picks a point to play from.
const noteView = (() => {
  const MERGED = 1, ABSORBED = 2, SHORTENED = 4, LENGTHENED = 8, LIMITED = 16, DUPLICATE = 32;
  const KINDS = [
    { flag: MERGED, label: "merged" },
    { flag: SHORTENED, label: "shortened" },
    { flag: LENGTHENED, label: "lengthened" },
    { flag: LIMITED, label: "velocity limited" },
    { flag: DUPLICATE, label: "duplicates" }
  ];
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
  const COLORS = {
    background: "#fff", blackRow: "#f4f3ef", line: "#e1e0d9", text: "#5f5e5a",
    written: "#8a8984", played: "#2a78d6", changed: "#e07b28", merged: "#d03b3b", playhead: "#1d1c1a"
  };
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
  let playing = false;
  // The playhead runs on the page's own clock, nudged toward each status
  // poll, so poll delays don't make it jump.
  let clockMs = 0, clockAt = 0;
  let marker = 0;        // where Play starts, when stopped
  let selected = null;   // the note stepped to
  let hovered = null;
  let pickHandler = () => {};
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
    const rowHeight = Math.max(6, Math.min(16, Math.floor(560 / rows)));
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
    }
    const shift = lead(); // notes are drawn where they sound: lead after the MIDI
    const keysHeight = l.rows * l.rowHeight;

    ctx.fillStyle = COLORS.background;
    ctx.fillRect(0, 0, l.width, l.height);
    ctx.font = "10px sans-serif";
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
      const start = (note.changes & MERGED ? note.on : Math.min(note.on, note.playedOn)) + shift;
      if (start > viewEnd || end < viewStart) continue;
      const y = yOf(note.key, l);
      const x = xOf(note.on + shift, l);
      if (note.changes & MERGED) {
        ctx.fillStyle = COLORS.merged + "40";
        ctx.fillRect(x, y + 0.5, Math.max(xOf(note.writtenOff + shift, l) - x, 2), l.rowHeight - 1);
        ctx.strokeStyle = COLORS.merged;
        ctx.lineWidth = 1.5;
        ctx.strokeRect(x, y + 0.75, Math.max(xOf(note.writtenOff + shift, l) - x, 2), l.rowHeight - 1.5);
        continue;
      }
      const changed = note.changes & (SHORTENED | LENGTHENED | ABSORBED | LIMITED);
      const playedX = xOf(note.playedOn + shift, l);
      ctx.fillStyle = (changed ? RAMPS.changed : RAMPS.played)[note.playedVelocity];
      ctx.fillRect(playedX, y + 1, Math.max(xOf(note.playedOff + shift, l) - playedX, 2), l.rowHeight - 2);
      ctx.strokeStyle = COLORS.written;
      ctx.lineWidth = 1;
      ctx.strokeRect(x + 0.5, y + 0.5, Math.max(xOf(note.writtenOff + shift, l) - x - 1, 1), l.rowHeight - 1);
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
          ctx.fillStyle = COLORS.played + "70";
          ctx.fillRect(x1, y + 1, Math.max(x2 - x1, 1), PEDAL_ROW - 2);
        } else {
          ctx.strokeStyle = COLORS.written;
          ctx.lineWidth = 1;
          ctx.strokeRect(x1 + 0.5, y + 1.5, Math.max(x2 - x1 - 1, 1), PEDAL_ROW - 3);
        }
      }
    };
    band(data.writtenPedal, 0, false);
    band(data.playedPedal, 1, true);

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
    const velocity = note.changes & (MERGED | DUPLICATE) ? note.writtenVelocity
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

  // ---- stepping through changes ----
  function kindNotes() {
    const flag = Number(kindSelect.value);
    return notes.filter((note) => note.changes & flag);
  }

  function showSelected() {
    if (!selected) {
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
    showSelected();
    redraw();
  }

  function fillKinds() {
    const chosen = kindSelect.value;
    kindSelect.replaceChildren(...KINDS.map((kind) => {
      const option = document.createElement("option");
      option.value = kind.flag;
      option.textContent = `${kind.label} (${notes.filter((n) => n.changes & kind.flag).length})`;
      return option;
    }));
    if (chosen) kindSelect.value = chosen;
  }

  kindSelect.addEventListener("change", () => { selected = null; showSelected(); redraw(); });
  prevButton.addEventListener("click", () => step(-1));
  nextButton.addEventListener("click", () => step(1));

  // ---- dragging, clicking, zooming, hovering ----
  let drag = null;
  canvas.addEventListener("pointerdown", (event) => {
    drag = { x: event.clientX, viewStart, moved: false };
    canvas.setPointerCapture(event.pointerId);
  });
  canvas.addEventListener("pointermove", (event) => {
    const rect = canvas.getBoundingClientRect();
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
    if (drag && !drag.moved) {
      const rect = canvas.getBoundingClientRect();
      const t = timeOf(event.clientX - rect.left, layout());
      if (event.clientX - rect.left >= GUTTER) pickHandler(Math.max(0, Math.round(t)));
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
    // Loads the loaded piece's notes at the current volume; hides the view without one.
    async load() {
      try {
        const response = await fetch("/api/piece/notes");
        data = response.ok ? await response.json() : null;
      } catch (error) {
        data = null;
      }
      section.hidden = !data;
      if (!data) return;
      notes = data.notes.map(([key, on, writtenOff, playedOn, playedOff, writtenVelocity, playedVelocity, changes,
        mergedInto]) => ({ key, on, writtenOff, playedOn, playedOff, writtenVelocity, playedVelocity, changes, mergedInto }));
      const keys = notes.map((n) => n.key);
      lowKey = Math.max(0, Math.min(...keys) - 1);
      highKey = Math.min(87, Math.max(...keys) + 1);
      const wasSelected = selected;
      selected = wasSelected ? notes.find((n) => n.key === wasSelected.key && n.on === wasSelected.on) || null : null;
      if (selected && !(selected.changes & Number(kindSelect.value))) selected = null;
      fillKinds();
      showSelected();
      redraw();
    },
    // From each status poll: the playback state, and the position on the
    // playback timeline at sampledAt (performance.now(), half way through the
    // request). A jump (play, seek, resume) moves the clock at once; small
    // differences, from how long replies take, are eased out.
    update(isPlaying, positionMs, startFrom, sampledAt) {
      const started = isPlaying && !playing;
      playing = isPlaying;
      const now = performance.now();
      const reported = positionMs + (now - sampledAt);
      const error = reported - (clockMs + (now - clockAt));
      clockMs = started || Math.abs(error) > 250 ? reported : clockMs + (now - clockAt) + error * 0.15;
      clockAt = now;
      if (!playing && startFrom !== marker) {
        marker = startFrom;
        if (marker < viewStart || marker > viewStart + span) viewStart = marker - span * 0.25;
      }
      if (started || !playing) redraw();
    },
    // Called with a time on the playback timeline when the view is clicked.
    onPick(handler) {
      pickHandler = handler;
    }
  };
})();
