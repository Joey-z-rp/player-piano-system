// The Live page: a MIDI keyboard on this computer (Web MIDI) plays the piano
// over Wi-Fi. Each message goes to the piano at once, with when it was played
// (/ws/live, web/live_socket.h); the piano plays it the chosen delay later.
// Optionally heard here at once too (synth.js). Uses ui (/assets/app.js).
(() => {
  const $ = (id) => document.getElementById(id);
  const inputSelect = $("midi-input");
  const startButton = $("live-start");
  const stateChip = $("live-state");
  const stats = $("live-stats");
  const warning = $("live-warning");
  const problem = $("midi-problem");
  const delayRange = $("delay");
  const soundBox = $("local-sound");
  const volumeRange = $("local-volume");
  const canvas = $("live-keys");
  const PING_MS = 500;
  const LANDING_ALLOWANCE_MS = 150; // the slowest hammer, about: the delay needs this on top of the network's
  const ALL = "all";

  // ---- remembered here ----
  const saved = (() => {
    try { return JSON.parse(localStorage.getItem("live") || "{}"); } catch { return {}; }
  })();
  const settings = { input: ALL, delay: 200, sound: true, volume: 60, ...saved };
  const save = () => {
    try { localStorage.setItem("live", JSON.stringify(settings)); } catch { /* not available */ }
  };

  // ---- MIDI ----
  let midi = null;
  function explain(html) {
    problem.innerHTML = html;
    problem.hidden = false;
    inputSelect.disabled = true;
  }
  async function connectMidi() {
    if (!window.isSecureContext) {
      const origin = location.origin;
      explain(`<strong>Chrome needs a one-time setting to use MIDI keyboards here.</strong> It only allows them on secure
        pages, and the piano's page isn't one (it has no certificate). On this computer:
        <ol><li>Open <code>chrome://flags/#unsafely-treat-insecure-origin-as-secure</code> (copy it into the address bar).</li>
        <li>Add <code>${origin}</code> to the box, and set it to <em>Enabled</em>.</li>
        <li>Press <em>Relaunch</em>, and come back to this page.</li></ol>`);
      return;
    }
    if (!navigator.requestMIDIAccess) {
      explain("<strong>This browser can't use MIDI keyboards.</strong> Use Chrome or Edge on a computer, or Chrome on " +
        "Android. (Safari, and every browser on an iPhone or iPad, can't.)");
      return;
    }
    try {
      midi = await navigator.requestMIDIAccess();
    } catch {
      explain("<strong>MIDI wasn't allowed.</strong> Allow it in the browser's site settings (the icon left of the " +
        "address), then reload the page.");
      return;
    }
    midi.onstatechange = listInputs;
    listInputs();
    showButton();
  }

  function listInputs() {
    const inputs = [...midi.inputs.values()];
    const options = [ui.el("option", { value: ALL }, inputs.length ? "All keyboards" : "No keyboard found: plug one in"),
      ...inputs.map((input) => ui.el("option", { value: input.name }, input.name))];
    inputSelect.replaceChildren(...options);
    inputSelect.value = inputs.some((i) => i.name === settings.input) ? settings.input : ALL;
    inputSelect.disabled = !inputs.length;
    inputs.forEach((input) => {
      input.onmidimessage = (inputSelect.value === ALL || input.name === inputSelect.value) ? onMidi : null;
    });
    $("midi-hint").textContent = inputs.length
      ? `${inputs.length} MIDI ${inputs.length === 1 ? "input" : "inputs"} found. Plugging one in shows it here.`
      : "";
  }
  inputSelect.addEventListener("change", () => {
    settings.input = inputSelect.value;
    save();
    listInputs();
  });

  // ---- what's played ----
  const down = new Map(); // MIDI note -> velocity, as played here
  let pedalDown = false;
  function onMidi(event) {
    const [status, a = 0, b = 0] = event.data;
    const kind = status & 0xf0;
    const at = Math.round(event.timeStamp || performance.now());
    if (kind === 0x90 && b > 0) {
      down.set(a, b);
      if (settings.sound) liveSynth.noteOn(a, b);
    } else if (kind === 0x80 || kind === 0x90) {
      down.delete(a);
      liveSynth.noteOff(a);
    } else if (kind === 0xb0 && a === 64) {
      pedalDown = b >= 64;
      liveSynth.pedal(pedalDown);
    } else {
      return; // not for the piano
    }
    send({ m: [[at, status, a, b]] });
    drawKeys();
  }

  // ---- the connection ----
  let socket = null;
  let live = false;
  let pingTimer = 0;
  const trips = []; // recent round trips, ms
  let late = 0;
  const sendRaw = (message) => socket && socket.readyState === WebSocket.OPEN && socket.send(JSON.stringify(message));
  const send = (message) => live && sendRaw(message);

  function setState(text, kind = "") {
    stateChip.textContent = text;
    stateChip.className = "chip" + (kind ? " " + kind : "");
  }

  function start() {
    liveSynth.start(); // this click lets the page make sound
    setState("Connecting…");
    startButton.disabled = true;
    socket = new WebSocket(`ws://${location.host}/ws/live`);
    socket.onopen = () => sendRaw({ start: settings.delay });
    socket.onmessage = (event) => {
      let reply;
      try { reply = JSON.parse(event.data); } catch { return; }
      if (reply.started !== undefined) {
        live = true;
        late = 0;
        trips.length = 0;
        setState("Playing live", "accent");
        ping();
        pingTimer = setInterval(ping, PING_MS);
      } else if (reply.pong !== undefined) {
        trips.push(performance.now() - reply.pong);
        if (trips.length > 20) trips.shift();
        late = reply.late;
        showStats();
      } else if (reply.stopped) {
        finish({ heat: "Stopped: a solenoid reached its heat limit", quiet: "Stopped: the connection went quiet",
          stopped: "Not playing" }[reply.stopped] || "Not playing", reply.stopped === "heat" ? "danger" : "");
      } else if (reply.error) {
        ui.toast(reply.error, { error: true });
        finish("Not playing");
      }
      showButton();
    };
    socket.onclose = () => {
      if (live) finish("Connection lost: press Start to play again", "warn");
      socket = null;
      showButton();
    };
    showButton();
  }

  function stop() {
    sendRaw({ stop: true });
    finish("Not playing");
  }

  function finish(text, kind) {
    live = false;
    clearInterval(pingTimer);
    if (socket) {
      const closing = socket;
      socket = null;
      closing.onclose = null;
      setTimeout(() => closing.close(), 100); // after the stop has gone
    }
    setState(text, kind);
    stats.textContent = "";
    warning.hidden = true;
    showButton();
  }

  function ping() {
    sendRaw({ ping: Math.round(performance.now()) });
  }

  function showStats() {
    const sorted = [...trips].sort((a, b) => a - b);
    const typical = sorted[Math.floor(sorted.length / 2)] / 2, worst = sorted[sorted.length - 1] / 2;
    stats.textContent = `Network ${Math.round(typical)} ms (up to ${Math.round(worst)})` +
      (late ? ` · ${late} ${late === 1 ? "note" : "notes"} came too late` : "");
    // The delay has to cover the slowest hammer and the network's worst moments.
    const short = settings.delay < LANDING_ALLOWANCE_MS + (worst - typical) + 20;
    warning.hidden = !short && !late;
    warning.textContent = short || late
      ? `Some notes may come too late for a ${settings.delay} ms delay, and sound late or uneven: try ` +
        `${Math.ceil((LANDING_ALLOWANCE_MS + (worst - typical) + 40) / 10) * 10} ms or more.`
      : "";
  }

  function showButton() {
    const connecting = socket && !live;
    const playing = ui.status?.playback?.state;
    startButton.disabled = Boolean(connecting) || (!live && (!midi || playing === "playing" || playing === "paused" ||
      playing === "testing"));
    startButton.textContent = live ? "Stop" : "Start playing live";
    startButton.classList.toggle("btn-primary", !live);
    startButton.classList.toggle("btn-danger", live);
    if (!live && !connecting && (playing === "playing" || playing === "paused")) {
      setState("A piece is playing: stop it to play live");
    }
  }
  startButton.addEventListener("click", () => (live ? stop() : start()));
  ui.onStatus(showButton);
  window.addEventListener("pagehide", () => live && stop());

  // ---- settings ----
  function showDelay() {
    $("delay-out").textContent = `${settings.delay} ms`;
    delayRange.value = settings.delay;
  }
  delayRange.addEventListener("input", () => {
    settings.delay = Number(delayRange.value);
    showDelay();
  });
  delayRange.addEventListener("change", () => {
    save();
    send({ delay: settings.delay });
    if (trips.length) showStats();
  });
  soundBox.checked = settings.sound;
  soundBox.addEventListener("change", () => {
    settings.sound = soundBox.checked;
    save();
    if (settings.sound) liveSynth.start();
    else liveSynth.allOff();
  });
  function showVolume() {
    $("local-volume-out").textContent = `${settings.volume}%`;
    volumeRange.value = settings.volume;
    liveSynth.setVolume(settings.volume / 100);
  }
  volumeRange.addEventListener("input", () => {
    settings.volume = Number(volumeRange.value);
    showVolume();
    save();
  });
  // Sound can only start from a click: the first one anywhere does.
  document.addEventListener("pointerdown", () => settings.sound && liveSynth.start(), { once: true });

  // ---- the keys being played ----
  const WHITE = [0, 2, 4, 5, 7, 9, 11];
  function drawKeys() {
    const width = canvas.clientWidth || 600, height = Math.max(60, Math.min(110, width / 9));
    const ratio = window.devicePixelRatio || 1;
    canvas.width = Math.round(width * ratio);
    canvas.height = Math.round(height * ratio);
    canvas.style.height = height + "px";
    const ctx = canvas.getContext("2d");
    ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
    const whites = [];
    for (let note = 21; note <= 108; note++) if (WHITE.includes(note % 12)) whites.push(note);
    const w = width / whites.length;
    const lit = ui.token("--accent");
    ctx.fillStyle = ui.token("--key-border") || "#999";
    ctx.fillRect(0, 0, width, height);
    whites.forEach((note, i) => {
      ctx.fillStyle = down.has(note) ? lit : ui.token("--key-white") || "#fff";
      ctx.fillRect(i * w + 0.5, 0, w - 1, height - 1);
    });
    whites.forEach((note, i) => {
      const black = note + 1;
      if (black > 108 || WHITE.includes(black % 12)) return;
      ctx.fillStyle = down.has(black) ? lit : ui.token("--key-black") || "#222";
      ctx.fillRect((i + 1) * w - w * 0.3, 0, w * 0.6, height * 0.62);
    });
    if (pedalDown) {
      ctx.fillStyle = lit;
      ctx.fillRect(0, height - 4, width, 4);
    }
  }
  new ResizeObserver(drawKeys).observe(canvas);
  ui.onTheme(drawKeys);

  showDelay();
  showVolume();
  showButton();
  connectMidi();
})();
