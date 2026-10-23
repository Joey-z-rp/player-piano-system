// The heat view: each key tinted by its solenoid's estimated heat (its rise
// above the room, shown as "+N °C"), from clear through yellow to red (at the
// limit), with the warmest key and the pedal's two solenoids below. While it's on, /api/heat is read once a
// second. Uses ui, and the keyboard, settings and save() from app.js.
(() => {
  const toggle = $("show-heat");
  const bar = $("heat-bar");
  let timer = 0;

  // A load (rise / limit) as a colour: none when cold, red at the limit.
  function heatColour(load) {
    if (!(load > 0.02)) return "transparent";
    const l = Math.min(load, 1);
    return `hsla(${55 - 55 * l}, 95%, ${55 - 7 * l}%, ${0.15 + 0.8 * l})`;
  }

  async function update() {
    let heat;
    try {
      heat = await ui.api("/api/heat");
    } catch {
      return; // the connection banner says why
    }
    const watchedKeys = heat.keys.some((r) => r !== null);
    const watchedPedal = heat.pedal.some((r) => r !== null);
    keyboard.querySelectorAll("[data-key]").forEach((button) => {
      const rise = heat.keys[Number(button.dataset.key)];
      button.style.setProperty("--heat", rise === null ? "transparent" : heatColour(rise / heat.limitC));
      const name = noteName(Number(button.dataset.key));
      button.title = rise === null ? `${name} · key ${button.dataset.key}`
        : `${name} · key ${button.dataset.key} · +${rise.toFixed(1)} °C above the room (limit +${heat.limitC} °C)`;
    });
    if (!watchedKeys && !watchedPedal) {
      bar.replaceChildren(ui.el("span", {}, "No coil is measured yet, so there's no heat to show. Enter the coils' numbers in "),
        ui.el("a", { href: "/settings/#heat" }, "Settings → Solenoid heat"), ".");
      return;
    }
    const items = [ui.el("span", { class: "heat-scale" }, "room", ui.el("i"), `+${heat.limitC} °C (limit)`)];
    if (watchedKeys) {
      let hottest = 0;
      heat.keys.forEach((r, i) => { if (r > heat.keys[hottest]) hottest = i; });
      items.push(ui.el("span", {}, heat.keys[hottest] > 0.05
        ? `Warmest key: ${noteName(hottest)}, +${heat.keys[hottest].toFixed(1)} °C` : "Keys: at room temperature"));
    } else {
      items.push(ui.el("span", {}, "Key coils not measured"));
    }
    if (watchedPedal) {
      heat.pedal.forEach((rise, i) => items.push(ui.el("span", { class: "heat-chip" },
        ui.el("i", { style: `background:${heatColour(rise / heat.limitC)}` }), `Pedal ${i + 1}: +${rise.toFixed(1)} °C`)));
    } else {
      items.push(ui.el("span", {}, "Pedal coils not measured"));
    }
    bar.replaceChildren(...items);
  }

  function show() {
    const on = toggle.checked;
    keyboard.classList.toggle("show-heat", on);
    bar.hidden = !on;
    clearInterval(timer);
    timer = 0;
    if (on) {
      update();
      timer = setInterval(() => { if (!document.hidden) update(); }, 1000);
    } else {
      keyboard.querySelectorAll("[data-key]").forEach((button) => {
        button.title = `${noteName(Number(button.dataset.key))} · key ${button.dataset.key}`;
      });
    }
  }

  toggle.checked = Boolean(settings.showHeat);
  toggle.addEventListener("change", () => {
    settings.showHeat = toggle.checked;
    save();
    show();
  });
  show();
})();
