// What every page shares (window.ui): the shell (header and tab bar,
// mini-player, banners), icons, messages, dialogs and menus, the API, and the
// live status from the controller (/api/events, else polling /api/status).
// Pages put their content in <main class="page"> and say which section they
// are with <body data-section="play|keys|settings">; this script is loaded
// before theirs.
const ui = (() => {
  // ---- icons (24 px, drawn with the current colour) ----
  const PATHS = {
    play: '<path d="M8 5.5v13l10.5-6.5z" fill="currentColor" stroke="none"/>',
    pause: '<rect x="6.5" y="5" width="4" height="14" rx="1" fill="currentColor" stroke="none"/><rect x="13.5" y="5" width="4" height="14" rx="1" fill="currentColor" stroke="none"/>',
    stop: '<rect x="6" y="6" width="12" height="12" rx="1.5" fill="currentColor" stroke="none"/>',
    music: '<path d="M9 18V6l11-2v12"/><circle cx="6" cy="18" r="3"/><circle cx="17" cy="16" r="3"/>',
    keys: '<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M9 4v16M15 4v16"/><path d="M7 4v9h4V4M13 4v9h4V4" fill="currentColor"/>',
    settings: '<circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.7 1.7 0 0 0 .3 1.8l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.7 1.7 0 0 0-1.8-.3 1.7 1.7 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1a1.7 1.7 0 0 0-1.1-1.5 1.7 1.7 0 0 0-1.8.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.7 1.7 0 0 0 .3-1.8 1.7 1.7 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1a1.7 1.7 0 0 0 1.5-1.1 1.7 1.7 0 0 0-.3-1.8l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.7 1.7 0 0 0 1.8.3H9a1.7 1.7 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.7 1.7 0 0 0 1 1.5 1.7 1.7 0 0 0 1.8-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.7 1.7 0 0 0-.3 1.8V9a1.7 1.7 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.7 1.7 0 0 0-1.5 1z"/>',
    library: '<path d="M4 5h3v14H4zM9.5 5h3v14h-3z"/><path d="M15 5.8l2.9-.8 3.6 13.5-2.9.8z"/>',
    plus: '<path d="M12 5v14M5 12h14"/>',
    minus: '<path d="M5 12h14"/>',
    more: '<circle cx="5" cy="12" r="1.6" fill="currentColor" stroke="none"/><circle cx="12" cy="12" r="1.6" fill="currentColor" stroke="none"/><circle cx="19" cy="12" r="1.6" fill="currentColor" stroke="none"/>',
    download: '<path d="M12 4v11M7 10l5 5 5-5M5 20h14"/>',
    upload: '<path d="M12 20V9M7 14l5-5 5 5M5 4h14"/>',
    trash: '<path d="M4 7h16M10 11v6M14 11v6M6 7l1 13h10l1-13M9 7V4h6v3"/>',
    edit: '<path d="M4 20h4L19 9l-4-4L4 16z"/><path d="M13.5 6.5l4 4"/>',
    search: '<circle cx="11" cy="11" r="6.5"/><path d="M20 20l-4.2-4.2"/>',
    close: '<path d="M6 6l12 12M18 6L6 18"/>',
    check: '<path d="M5 12.5l4.5 4.5L19 7.5"/>',
    left: '<path d="M15 5l-7 7 7 7"/>',
    right: '<path d="M9 5l7 7-7 7"/>',
    alert: '<path d="M12 3l9.5 17h-19z"/><path d="M12 10v4.5M12 17.5v.01"/>',
    flame: '<path d="M12 21c4 0 6.5-2.7 6.5-6.3 0-3.9-3-6-4.2-9.7-2.2 1.6-3.3 3.8-3.1 6.1-1.3-.6-2.2-1.9-2.4-3.4C6.8 9.6 5.5 12 5.5 14.7 5.5 18.3 8 21 12 21z"/>',
    wifi: '<path d="M2.5 9a14 14 0 0 1 19 0M5.5 12.3a9.5 9.5 0 0 1 13 0M8.6 15.6a5 5 0 0 1 6.8 0"/><circle cx="12" cy="19" r="1.2" fill="currentColor" stroke="none"/>',
    sun: '<circle cx="12" cy="12" r="4.5"/><path d="M12 2v2M12 20v2M2 12h2M20 12h2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/>',
    info: '<circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7.5v.01"/>',
    refresh: '<path d="M20 11a8 8 0 1 0-2.3 5.7M20 4v7h-7"/>',
    pedal: '<path d="M5 19h14M8 19l2.5-10h3L16 19"/><path d="M10.5 9h3V5h-3z"/>',
    volume: '<path d="M4 9.5h3.5L12 6v12l-4.5-3.5H4z"/><path d="M15.5 9a4 4 0 0 1 0 6M18 6.5a7.5 7.5 0 0 1 0 11"/>',
    mute: '<path d="M4 9.5h3.5L12 6v12l-4.5-3.5H4z"/><path d="M16 9.5l5 5M21 9.5l-5 5"/>',
  };
  function icon(name, label) {
    const a11y = label ? `role="img" aria-label="${label}"` : 'aria-hidden="true"';
    return `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" ${a11y}>${PATHS[name] || ""}</svg>`;
  }
  const LOGO = `<svg viewBox="0 0 32 32" aria-hidden="true"><rect width="32" height="32" rx="7" fill="#1d1c1a"/>
    <rect x="5" y="7" width="22" height="18" rx="2" fill="#fff"/><path d="M12.3 7v18M19.7 7v18" stroke="#1d1c1a" stroke-width="1.2"/>
    <rect x="10.3" y="7" width="4" height="10" rx="1" fill="#1d1c1a"/><rect x="17.7" y="7" width="4" height="10" rx="1" fill="#1d1c1a"/>
    <rect x="5" y="7" width="22" height="2.4" fill="#2a63c9"/></svg>`;

  // ---- small helpers ----
  const $ = (id) => document.getElementById(id);
  function el(tag, props = {}, ...children) {
    const node = document.createElement(tag);
    for (const [key, value] of Object.entries(props)) {
      if (value === undefined || value === null || value === false) continue;
      if (key === "class") node.className = value;
      else if (key === "html") node.innerHTML = value;
      else if (key === "dataset") Object.assign(node.dataset, value);
      else if (key.startsWith("on")) node.addEventListener(key.slice(2), value);
      else if (key in node && key !== "list") node[key] = value;
      else node.setAttribute(key, value === true ? "" : value);
    }
    node.append(...children.flat().filter((c) => c !== null && c !== undefined && c !== false));
    return node;
  }
  function formatTime(ms) {
    const seconds = Math.max(0, Math.floor(ms / 1000));
    return Math.floor(seconds / 60) + ":" + String(seconds % 60).padStart(2, "0");
  }
  const NAMES = ["C", "C♯", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B"];
  const noteName = (midi) => NAMES[midi % 12] + (Math.floor(midi / 12) - 1);
  const keyName = (key) => noteName(key + 21);
  const typing = (target) => target && (target.isContentEditable || ["INPUT", "TEXTAREA", "SELECT"].includes(target.tagName));

  // ---- theme: auto (the device's), light or dark; set before the page draws ----
  function theme() {
    try { return localStorage.getItem("theme") || "auto"; } catch { return "auto"; }
  }
  const themeListeners = [];
  function setTheme(mode) {
    try { mode === "auto" ? localStorage.removeItem("theme") : localStorage.setItem("theme", mode); } catch { /* not kept */ }
    if (mode === "auto") delete document.documentElement.dataset.theme;
    else document.documentElement.dataset.theme = mode;
    themeListeners.forEach((fn) => fn());
  }
  window.matchMedia("(prefers-color-scheme: dark)").addEventListener("change", () => themeListeners.forEach((fn) => fn()));
  // A colour token's value now (for canvases and charts).
  const token = (name) => getComputedStyle(document.documentElement).getPropertyValue(name).trim();

  // ---- messages ----
  let toasts;
  function toast(text, { error = false, duration } = {}) {
    if (!text) return;
    const item = el("div", { class: "toast" + (error ? " error" : ""), role: error ? "alert" : "status",
      html: icon(error ? "alert" : "check") }, el("span", {}, text));
    const close = () => {
      item.classList.add("leaving");
      setTimeout(() => item.remove(), 220);
    };
    item.addEventListener("click", close);
    toasts.append(item);
    while (toasts.children.length > 3) toasts.firstElementChild.remove();
    setTimeout(close, duration ?? (error ? 9000 : 5000));
  }

  // ---- dialogs ----
  // Asks to confirm: resolves true or false.
  function confirm({ title, message = "", confirm: confirmLabel = "OK", danger = false, cancel = "Cancel" }) {
    return new Promise((resolve) => {
      const dialog = el("dialog", { class: "dialog" });
      const done = (result) => {
        dialog.close();
        dialog.remove();
        resolve(result);
      };
      const ok = el("button", { class: "btn " + (danger ? "btn-danger" : "btn-primary"), type: "button",
        onclick: () => done(true) }, confirmLabel);
      dialog.append(
        el("div", { class: "dialog-body" }, el("h2", {}, title), message ? el("p", {}, message) : null),
        el("div", { class: "dialog-actions" },
          el("button", { class: "btn", type: "button", onclick: () => done(false) }, cancel), ok));
      dialog.addEventListener("cancel", (event) => {
        event.preventDefault();
        done(false);
      });
      dialog.addEventListener("click", (event) => {
        if (event.target === dialog) done(false); // the backdrop
      });
      document.body.append(dialog);
      dialog.showModal();
      ok.focus();
    });
  }

  // ---- menus: [{label, icon, onclick, href, download, danger}] by an anchor ----
  let openMenu = null;
  function closeMenu() {
    if (openMenu) {
      openMenu.remove();
      openMenu = null;
    }
  }
  function menu(anchor, items) {
    closeMenu();
    const panel = el("div", { class: "menu", role: "menu" }, items.map((item) => {
      const props = { class: item.danger ? "danger" : "", role: "menuitem", html: icon(item.icon || "info") };
      const node = item.href
        ? el("a", { ...props, href: item.href, download: item.download })
        : el("button", { ...props, type: "button" });
      node.append(el("span", {}, item.label));
      node.addEventListener("click", () => {
        closeMenu();
        if (item.onclick) item.onclick();
      });
      return node;
    }));
    document.body.append(panel);
    const rect = anchor.getBoundingClientRect();
    const width = panel.offsetWidth, height = panel.offsetHeight;
    panel.style.left = Math.max(8, Math.min(rect.right - width, innerWidth - width - 8)) + "px";
    panel.style.top = (rect.bottom + height + 8 < innerHeight ? rect.bottom + 4 : Math.max(8, rect.top - height - 4)) + "px";
    openMenu = panel;
    panel.querySelector("button, a")?.focus();
    return panel;
  }
  document.addEventListener("pointerdown", (event) => {
    if (openMenu && !openMenu.contains(event.target)) closeMenu();
  }, true);
  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape") closeMenu();
  });
  window.addEventListener("scroll", closeMenu, true);
  window.addEventListener("resize", closeMenu);

  // ---- the API ----
  // Resolves with the reply's JSON; rejects with an Error saying why.
  async function api(url, { method, json, body, headers } = {}) {
    let response;
    try {
      response = await fetch(url, {
        method: method || (json !== undefined || body !== undefined ? "POST" : "GET"),
        headers: json !== undefined ? { "Content-Type": "application/json", ...headers } : headers,
        body: json !== undefined ? JSON.stringify(json) : body,
      });
    } catch {
      throw new Error("Couldn't reach the piano");
    }
    const reply = await response.json().catch(() => ({}));
    if (!response.ok) throw new Error(reply.error || `The piano replied ${response.status}`);
    return reply;
  }
  // As api, but an error is shown as a message: resolves null then.
  async function request(url, options) {
    try {
      return await api(url, options);
    } catch (error) {
      toast(error.message, { error: true });
      return null;
    }
  }

  // ---- the live status ----
  const statusListeners = [];
  let last = null, lastAt = 0, source = null, pollTimer = 0, offlineTimer = 0;
  function deliver(status) {
    last = status;
    lastAt = performance.now();
    setOnline(true);
    statusListeners.forEach((fn) => fn(status, lastAt));
  }
  // Calls fn(status, receivedAt) with every status, and the latest at once.
  function onStatus(fn) {
    statusListeners.push(fn);
    if (last) fn(last, lastAt);
  }
  // Fetches the status now (after an action, for an instant update).
  async function refreshStatus() {
    try {
      deliver(await api("/api/status"));
    } catch {
      setOnline(false);
    }
  }
  function startPolling() {
    if (pollTimer) return;
    pollTimer = setInterval(refreshStatus, 1000);
  }
  function connect() {
    if (!("EventSource" in window)) {
      startPolling();
      return;
    }
    source = new EventSource("/api/events");
    source.addEventListener("status", (event) => {
      clearInterval(pollTimer);
      pollTimer = 0;
      try { deliver(JSON.parse(event.data)); } catch { /* a bad message: the next will do */ }
    });
    source.addEventListener("error", () => {
      // It reconnects by itself; meanwhile poll, so the page isn't stuck.
      startPolling();
    });
  }
  // Offline: nothing heard for a while.
  let online = true;
  function setOnline(value) {
    clearTimeout(offlineTimer);
    if (value) offlineTimer = setTimeout(() => setOnline(false), 8000);
    if (value === online) return;
    online = value;
    showBanners();
  }

  // ---- the shell ----
  const SECTIONS = [
    { id: "play", label: "Play", href: "/play/", icon: "music" },
    { id: "keys", label: "Keys", href: "/keys/", icon: "keys" },
    { id: "settings", label: "Settings", href: "/settings/", icon: "settings" },
  ];
  const section = document.body.dataset.section || "";
  let banners, mini;
  function buildShell() {
    const links = (cls) => SECTIONS.map((s) => el("a", { href: s.href, "aria-current": s.id === section ? "page" : null,
      html: icon(s.icon) }, el("span", {}, s.label)));
    const header = el("header", { class: "app-header" },
      el("a", { class: "brand", href: "/play/", html: LOGO }, el("span", {}, "Player piano")),
      el("nav", { class: "nav-links", "aria-label": "Sections" }, links()),
      el("div", { class: "header-status", id: "header-status" }));
    banners = el("div", { class: "banners", "aria-live": "polite" });
    const tabbar = el("nav", { class: "tabbar", "aria-label": "Sections" }, links());
    toasts = el("div", { class: "toasts", "aria-live": "polite" });
    document.body.prepend(header, banners);
    document.body.append(tabbar, toasts);
    if (section !== "play") buildMiniPlayer();
  }

  // Banners for what concerns every page: no connection, the solenoids'
  // heat (while Play waits for them), an update under way.
  function heatText(heat) {
    const name = (channel) => (channel >= 88 ? "the pedal solenoids" : `${keyName(channel)}'s solenoid`);
    const minutes = (s) => (s < 60 ? "under a minute" : `about ${Math.ceil(s / 60)} min`);
    const degrees = (c) => `${c.toFixed(0)} °C`;
    const stopped = { paused: "Paused", testStopped: "Test stopped", released: "Let go" }[heat.action];
    return {
      hot: Boolean(stopped),
      cooling: heat.load >= heat.resumeLoad,
      text: stopped
        ? `${stopped}: ${name(heat.actionChannel)} reached the heat limit. Play can go on once ` +
          `${heat.actionChannel >= 88 ? "they've" : "it's"} cooled, in ${minutes(heat.coolS)}.`
        : `${name(heat.channel)[0].toUpperCase() + name(heat.channel).slice(1)} ${heat.channel >= 88 ? "are" : "is"} warm: ` +
          `${degrees(heat.riseC)} of the ${degrees(heat.limitC)} allowed. Play waits until under ` +
          `${degrees(heat.limitC * heat.resumeLoad)}, in ${minutes(heat.coolS)}.`,
      line: `Warmest solenoid: ${name(heat.channel)}, ${degrees(heat.riseC)} of ${degrees(heat.limitC)}`,
    };
  }
  function showBanners() {
    if (!banners) return;
    const items = [];
    if (!online) {
      items.push(el("div", { class: "banner danger", html: icon("wifi") },
        el("div", {}, el("strong", {}, "Can't reach the piano. "), "Retrying… If it's restarting, this clears in a moment.")));
    }
    const status = last;
    if (online && status) {
      const update = status.update || { kind: "none" };
      if (update.kind !== "none") {
        items.push(el("div", { class: "banner info", html: icon("download") },
          el("div", { style: "flex:1" },
            `Updating the ${update.kind === "firmware" ? "firmware" : "web pages"} over Wi-Fi: ${update.percent}%. ` +
            "The piano restarts when it's done.",
            el("div", { class: "progress" }, el("i", { style: `width:${update.percent}%` })))));
      }
      if (status.heat && status.heat.watched) {
        const heat = heatText(status.heat);
        if (heat.cooling) {
          items.push(el("div", { class: "banner" + (heat.hot ? " danger" : ""), html: icon("flame") }, el("div", {}, heat.text)));
        }
      }
    }
    banners.replaceChildren(...items);
    const headerStatus = $("header-status");
    if (headerStatus) {
      headerStatus.replaceChildren(el("span", { class: "dot" + (online ? "" : " off") }),
        el("span", { class: "label" }, online ? (status?.wifi?.setup ? "Setup network" : "Connected") : "Offline"));
    }
  }

  // The mini-player: the loaded piece, play/pause and stop, on every page but Play.
  function buildMiniPlayer() {
    const title = el("div", { class: "title" });
    const state = el("div", { class: "state" });
    const playButton = el("button", { class: "btn btn-primary btn-icon btn-round", type: "button", "aria-label": "Play" });
    const stopButton = el("button", { class: "btn btn-ghost btn-icon btn-round", type: "button", "aria-label": "Stop", html: icon("stop") });
    const bar = el("i", { class: "bar" });
    mini = el("div", { class: "mini-player", hidden: true },
      el("a", { class: "info", href: "/play/" }, title, state), playButton, stopButton, bar);
    document.body.append(mini);
    playButton.addEventListener("click", async () => {
      const playing = last?.playback?.state === "playing";
      await request(playing ? "/api/playback/pause" : "/api/playback/play", { method: "POST" });
      refreshStatus();
    });
    stopButton.addEventListener("click", async () => {
      await request("/api/playback/stop", { method: "POST" });
      refreshStatus();
    });
    onStatus((status) => {
      const playback = status.playback;
      const piece = playback.piece;
      mini.hidden = !piece;
      document.body.classList.toggle("has-mini", Boolean(piece));
      if (!piece) return;
      const playing = playback.state === "playing";
      title.textContent = piece.title;
      state.textContent = { playing: "Playing", paused: "Paused", testing: "Testing keys" }[playback.state] ||
        `Ready · ${formatTime(piece.durationMs)}`;
      if (playing || playback.state === "paused") state.textContent += ` · ${formatTime(playback.positionMs)} / ${formatTime(piece.durationMs)}`;
      playButton.innerHTML = icon(playing ? "pause" : "play");
      playButton.setAttribute("aria-label", playing ? "Pause" : playback.state === "paused" ? "Resume" : "Play");
      playButton.disabled = playback.building || playback.state === "testing";
      stopButton.disabled = playback.state === "idle";
      bar.style.width = `${Math.min(100, (100 * playback.positionMs) / Math.max(piece.durationMs, 1))}%`;
    });
  }

  buildShell();
  onStatus(showBanners);
  connect();
  refreshStatus();

  return {
    icon, el, $, formatTime, noteName, keyName, typing, toast, confirm, menu, closeMenu, api, request,
    onStatus, refreshStatus, get status() { return last; }, heatText,
    theme, setTheme, token, onTheme: (fn) => themeListeners.push(fn),
  };
})();
