// A simple piano-like sound, made in the browser (Web Audio), so what's
// played on the keyboard is heard at once while the piano plays it a moment
// later. No samples: a few sine and triangle partials, brighter and louder
// with velocity, dying away sooner up the keyboard, held by the sustain pedal.
const liveSynth = (() => {
  let context = null;
  let master = null;
  let volume = 0.6;
  let sustain = false;
  const voices = new Map(); // note -> {gain, oscillators}
  const sustained = new Set(); // notes let go while the pedal's down

  // Browsers only allow sound after a click: call from one.
  function start() {
    if (!context) {
      context = new AudioContext({ latencyHint: "interactive" });
      const limiter = context.createDynamicsCompressor();
      limiter.threshold.value = -10;
      limiter.ratio.value = 12;
      master = context.createGain();
      master.gain.value = volume;
      master.connect(limiter).connect(context.destination);
    }
    return context.resume();
  }

  const running = () => Boolean(context) && context.state === "running";

  function fadeOut(voice, seconds) {
    const t = context.currentTime;
    const gain = voice.gain.gain;
    if (gain.cancelAndHoldAtTime) gain.cancelAndHoldAtTime(t);
    else {
      gain.cancelScheduledValues(t);
      gain.setValueAtTime(gain.value, t);
    }
    gain.setTargetAtTime(0, t, seconds / 4);
    voice.oscillators.forEach((o) => o.stop(t + seconds * 1.5));
  }

  function release(note) {
    const voice = voices.get(note);
    if (!voice) return;
    voices.delete(note);
    sustained.delete(note);
    fadeOut(voice, 0.25);
  }

  function noteOn(note, velocity) {
    if (!running()) return;
    if (voices.has(note)) {
      fadeOut(voices.get(note), 0.03); // struck again: the last one stops
      voices.delete(note);
    }
    sustained.delete(note);
    const t = context.currentTime;
    const frequency = 440 * 2 ** ((note - 69) / 12);
    const strength = velocity / 127;
    const peak = 0.04 + strength ** 1.5 * 0.3;
    const ring = Math.min(6, Math.max(0.4, 3.5 * 2 ** (-(note - 60) / 24))); // seconds to die away
    const gain = context.createGain();
    gain.gain.setValueAtTime(0, t);
    gain.gain.linearRampToValueAtTime(peak, t + 0.004);
    gain.gain.setTargetAtTime(peak * 0.4, t + 0.004, 0.08);
    gain.gain.setTargetAtTime(0, t + 0.2, ring / 3);
    const filter = context.createBiquadFilter();
    filter.type = "lowpass";
    filter.frequency.value = Math.min(12000, frequency * (2 + 10 * strength));
    filter.connect(gain).connect(master);
    const oscillators = [[1, "triangle", 1], [2.002, "sine", 0.3], [3.003, "sine", 0.08]].map(([ratio, type, level]) => {
      const oscillator = context.createOscillator();
      oscillator.type = type;
      oscillator.frequency.value = frequency * ratio;
      const partial = context.createGain();
      partial.gain.value = level;
      oscillator.connect(partial).connect(filter);
      oscillator.start(t);
      oscillator.stop(t + ring * 4);
      return oscillator;
    });
    voices.set(note, { gain, oscillators });
  }

  function noteOff(note) {
    if (!voices.has(note)) return;
    if (sustain) sustained.add(note);
    else release(note);
  }

  function pedal(down) {
    sustain = down;
    if (!down) [...sustained].forEach(release);
  }

  function allOff() {
    sustain = false;
    [...voices.keys()].forEach(release);
  }

  function setVolume(value) {
    volume = value;
    if (master) master.gain.setTargetAtTime(value, context.currentTime, 0.02);
  }

  return { start, running, noteOn, noteOff, pedal, allOff, setVolume };
})();
