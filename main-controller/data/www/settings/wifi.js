// The Wi-Fi the piano joins: what it's on now, looking for networks, saving
// the one to join (it restarts to join it) and forgetting it. Uses ui.
(() => {
const wifiNow = document.getElementById("now");

async function showNow() {
  try {
    const { wifi } = await ui.api("/api/system");
    const joins = wifi.saved ? `“${wifi.joins}” (saved here)` : wifi.fallback ? `“${wifi.joins}” (from secrets.ini)` : "none saved";
    wifiNow.replaceChildren(ui.el("span", { html: ui.icon(wifi.setup ? "alert" : "wifi") }),
      ui.el("div", { class: "lines" },
        ui.el("strong", {}, wifi.setup ? `Its own network, “${wifi.network}”` : `On “${wifi.network}”`),
        ui.el("span", { class: "small muted" }, wifi.setup
          ? `At ${wifi.ip}: it couldn't join a network. Joins at start: ${joins}.`
          : `At ${wifi.ip} · signal ${wifi.rssi} dBm · joins at start: ${joins}`)));
    document.getElementById("forget").disabled = !wifi.saved;
    const ssid = document.getElementById("ssid");
    if (!ssid.value && wifi.joins) ssid.value = wifi.joins;
  } catch {
    wifiNow.replaceChildren(ui.el("span", { class: "small muted" }, "Couldn't reach the piano."));
  }
}

const bars = (rssi) => (rssi > -55 ? "▂▄▆█" : rssi > -67 ? "▂▄▆" : rssi > -78 ? "▂▄" : "▂");

async function scan() {
  const button = document.getElementById("scan"), status = document.getElementById("scan-status");
  button.disabled = true;
  status.hidden = false;
  try {
    for (let tries = 0; tries < 20; tries++) {
      const reply = await ui.api("/api/wifi/scan");
      if (reply.networks) {
        const list = document.getElementById("networks");
        list.replaceChildren(...reply.networks.map((n) => {
          const choose = ui.el("button", { type: "button" },
            ui.el("span", { class: "name" }, n.ssid + (n.secure ? "" : " (open)")),
            ui.el("span", { class: "signal" }, `${bars(n.rssi)} ${n.rssi} dBm`));
          choose.addEventListener("click", () => {
            list.querySelectorAll(".chosen").forEach((b) => b.classList.remove("chosen"));
            choose.classList.add("chosen");
            document.getElementById("ssid").value = n.ssid;
            document.getElementById("password").value = "";
            document.getElementById("password").focus();
          });
          return ui.el("li", {}, choose);
        }));
        if (!reply.networks.length) ui.toast("No networks found");
        break;
      }
      await new Promise((resolve) => setTimeout(resolve, 700));
    }
  } catch (error) {
    ui.toast(`Couldn't look for networks: ${error.message}`, { error: true });
  }
  button.disabled = false;
  status.hidden = true;
}

document.getElementById("scan").addEventListener("click", scan);
document.getElementById("show-password").addEventListener("change", (event) => {
  document.getElementById("password").type = event.target.checked ? "text" : "password";
});

document.getElementById("save-wifi").addEventListener("click", async () => {
  const ssid = document.getElementById("ssid").value.trim(), password = document.getElementById("password").value;
  if (!ssid) return ui.toast("Choose or type a network", { error: true });
  const sure = await ui.confirm({ title: `Join “${ssid}”?`, message: "The piano restarts to join it. If it can't, " +
    "its own network, “Piano setup”, comes back so you can try again.", confirm: "Save and join" });
  if (!sure) return;
  if (await ui.request("/api/wifi", { json: { ssid, password } })) {
    ui.toast(`Saved. The piano is restarting to join “${ssid}”: find it there at http://piano.local/ in a minute.`,
      { duration: 15000 });
    document.getElementById("save-wifi").disabled = true;
  }
});

document.getElementById("forget").addEventListener("click", async () => {
  const sure = await ui.confirm({ title: "Forget the saved network?", message: "It joins the one in secrets.ini instead, if there is one.",
    confirm: "Forget", danger: true });
  if (!sure) return;
  const reply = await ui.request("/api/wifi/forget", { method: "POST" });
  if (reply) {
    ui.toast(!reply.joins ? "Forgotten. There's no other network to join, so it stays on “Piano setup”."
      : reply.tryingNow ? `Forgotten. It's joining “${reply.joins}” now: “Piano setup” goes away once it has.`
      : `Forgotten. It joins “${reply.joins}” from the next start.`, { duration: 10000 });
  }
  showNow();
});

showNow();
})();
