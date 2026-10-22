// The Settings page's appearance (the theme) and About sections, and its
// section list, which follows the section in view. Uses ui.
(() => {
  // ---- appearance ----
  document.querySelectorAll('input[name="theme"]').forEach((radio) => {
    radio.checked = radio.value === ui.theme();
    radio.addEventListener("change", () => ui.setTheme(radio.value));
  });

  // ---- about ----
  async function showAbout() {
    let system = null, library = null;
    try {
      [system, library] = await Promise.all([ui.api("/api/system"), ui.api("/api/library").catch(() => null)]);
    } catch {
      document.getElementById("about-list").replaceChildren(ui.el("dt", {}, "Piano"), ui.el("dd", {}, "Couldn't reach it"));
      return;
    }
    const { firmware, wifi } = system;
    const warning = document.getElementById("firmware-warning");
    warning.hidden = !firmware.rolledBack && !firmware.onTrial;
    warning.className = "banner" + (firmware.rolledBack ? " danger" : " info");
    warning.textContent = firmware.rolledBack
      ? "The last firmware update didn't get online, so the piano went back to this firmware."
      : "Running a new firmware update: it's kept once it's online.";
    const mb = (bytes) => (bytes / 1048576).toFixed(1);
    const rows = [
      ["Firmware", firmware.version],
      ["Built", firmware.built],
      ["Running from", firmware.slot + (firmware.onTrial ? " (on trial)" : "")],
      ["Network", wifi.setup ? `its own, “${wifi.network}”` : `“${wifi.network}”, ${wifi.rssi} dBm`],
      ["Address", `${wifi.ip} · piano.local`],
      ["Library", library ? `${library.pieces.length} pieces · ${mb(library.usedBytes)} of ${mb(library.totalBytes)} MB used` : "not available"],
    ];
    document.getElementById("about-list").replaceChildren(...rows.flatMap(([term, value]) =>
      [ui.el("dt", {}, term), ui.el("dd", {}, value)]));
  }
  showAbout();

  // ---- the section list follows the section in view ----
  const links = [...document.querySelectorAll("#subnav a")];
  const current = (id) => links.forEach((a) => a.classList.toggle("current", a.getAttribute("href") === "#" + id));
  const seen = new Map();
  const observer = new IntersectionObserver((entries) => {
    entries.forEach((entry) => seen.set(entry.target.id, entry.isIntersecting ? 1 : 0));
    const top = links.map((a) => a.getAttribute("href").slice(1)).find((id) => seen.get(id));
    if (top) {
      current(top);
      // On narrow screens the chips scroll sideways: keep the current one in view.
      if (matchMedia("(max-width: 860px)").matches) {
        links.find((a) => a.classList.contains("current"))?.scrollIntoView({ block: "nearest", inline: "nearest" });
      }
    }
  }, { rootMargin: "-90px 0px -55% 0px" });
  links.forEach((a) => {
    const section = document.getElementById(a.getAttribute("href").slice(1));
    if (section) observer.observe(section);
  });
  current((location.hash || "#rules").slice(1));
})();
