// The listening tests: velocity steps on a key, a velocity across keys, and a
// repeat, each played as a piece plays them (playNotes in app.js).
// ---- listening tests ----
function bind(id, name, parse = Number) {
  const input = $(id);
  input.value = settings[name];
  input.addEventListener("change", () => { settings[name] = parse(input.value); save(); });
  return input;
}
bind("ramp-velocities", "rampVelocities", String);
bind("ramp-gap", "rampGap");
bind("range-from", "rangeFrom");
bind("range-to", "rangeTo");
bind("range-gap", "rangeGap");
bind("repeat-first", "repeatFirst");
bind("repeat-second", "repeatSecond");
bind("repeat-interval", "repeatInterval");

$("ramp").addEventListener("click", () => {
  const velocities = settings.rampVelocities.split(/[\s,]+/).map(Number).filter((v) => v >= 1 && v <= 127);
  if (!velocities.length) return write("velocity steps: give velocities from 1 to 127, e.g. 16, 32, 64");
  const gap = settings.rampGap, length = Math.min(hold(), gap - 20);
  playNotes(velocities.map((v, i) => [settings.key, v, i * gap, i * gap + length]),
    `${noteName(settings.key)} at velocities ${velocities.join(", ")}`);
});

$("range").addEventListener("click", () => {
  const from = Math.min(settings.rangeFrom, settings.rangeTo), to = Math.max(settings.rangeFrom, settings.rangeTo);
  const gap = settings.rangeGap, length = Math.min(hold(), gap - 20);
  const notes = [];
  for (let key = from; key <= to; key++) notes.push([key, settings.velocity, (key - from) * gap, (key - from) * gap + length]);
  playNotes(notes, `${noteName(from)} to ${noteName(to)} at velocity ${settings.velocity}`);
});

$("repeat").addEventListener("click", async () => {
  const { repeatFirst: first, repeatSecond: second, repeatInterval: interval } = settings;
  const reply = await playNotes([[settings.key, first, 0, interval], [settings.key, second, interval, interval + hold()]],
    `${noteName(settings.key)}: ${first} then ${second}, ${interval} ms apart`);
  if (reply) {
    $("repeat-result").textContent = reply.merged
      ? `Merged into one note: ${interval} ms is too soon for the config's repeat limits at these velocities.`
      : "Struck twice, as a piece would.";
  }
});

$("stop").addEventListener("click", () => {
  letPedalUp();
  stopFlashes();
  post("/api/playback/stop", undefined, "stopped");
});
