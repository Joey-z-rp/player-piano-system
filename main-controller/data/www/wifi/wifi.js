// Shows the controller's Wi-Fi, looks for networks, and saves the one to join.
const $ = (id) => document.getElementById(id);

function say(text, error = false) {
  $("message").textContent = text;
  $("message").className = error ? "error" : "";
}

async function showNow() {
  try {
    const { wifi } = await (await fetch("/api/system")).json();
    const joins = wifi.saved ? `“${wifi.joins}” (saved here)` : wifi.fallback ? `“${wifi.joins}” (from secrets.ini)` : "none saved";
    $("now").innerHTML = "";
    const line = (text) => $("now").append(text, document.createElement("br"));
    line(wifi.setup ? `Running its own network, “${wifi.network}”, at ${wifi.ip}: it couldn't join one.`
      : `On “${wifi.network}” at ${wifi.ip}, signal ${wifi.rssi} dBm.`);
    line(`Joins at start: ${joins}.`);
    $("forget").disabled = !wifi.saved;
    if (!$("ssid").value && wifi.joins) $("ssid").value = wifi.joins;
  } catch {
    $("now").textContent = "Couldn't reach the controller.";
  }
}

const bars = (rssi) => (rssi > -55 ? "▂▄▆█" : rssi > -67 ? "▂▄▆" : rssi > -78 ? "▂▄" : "▂");

async function scan() {
  $("scan").disabled = true;
  $("scan-status").textContent = "Looking…";
  try {
    for (let tries = 0; tries < 20; tries++) {
      const reply = await (await fetch("/api/wifi/scan")).json();
      if (reply.networks) {
        $("networks").replaceChildren(...reply.networks.map((n) => {
          const li = document.createElement("li");
          const button = document.createElement("button");
          button.type = "button";
          const name = document.createElement("span");
          name.textContent = n.ssid + (n.secure ? "" : " (open)");
          const signal = document.createElement("span");
          signal.className = "bars";
          signal.textContent = `${bars(n.rssi)} ${n.rssi} dBm`;
          button.append(name, signal);
          button.addEventListener("click", () => {
            $("networks").querySelectorAll(".chosen").forEach((b) => b.classList.remove("chosen"));
            button.classList.add("chosen");
            $("ssid").value = n.ssid;
            $("password").value = "";
            $("password").focus();
          });
          li.append(button);
          return li;
        }));
        $("scan-status").textContent = reply.networks.length ? "" : "No networks found.";
        break;
      }
      await new Promise((r) => setTimeout(r, 700));
    }
  } catch {
    $("scan-status").textContent = "Couldn't look for networks.";
  }
  $("scan").disabled = false;
}

$("scan").addEventListener("click", scan);
$("show-password").addEventListener("change", () => {
  $("password").type = $("show-password").checked ? "text" : "password";
});

$("save").addEventListener("click", async () => {
  const ssid = $("ssid").value.trim(), password = $("password").value;
  if (!ssid) return say("Choose or type a network", true);
  try {
    const response = await fetch("/api/wifi", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ ssid, password }),
    });
    const reply = await response.json();
    if (!response.ok) return say(reply.error, true);
    say(`Saved. The controller is restarting to join “${ssid}”: find it there at http://piano.local/ in a minute.`);
    $("save").disabled = true;
  } catch {
    say("Couldn't reach the controller", true);
  }
});

$("forget").addEventListener("click", async () => {
  if (!confirm("Forget the saved network? It joins the one in secrets.ini instead, if there is one.")) return;
  try {
    const reply = await (await fetch("/api/wifi/forget", { method: "POST" })).json();
    say(!reply.joins ? "Forgotten. There's no other network to join, so it stays on “Piano setup”."
      : reply.tryingNow ? `Forgotten. It's joining “${reply.joins}” now: “Piano setup” goes away once it has, ` +
        "and it's then at http://piano.local/."
      : `Forgotten. It joins “${reply.joins}” from the next start.`);
  } catch {
    say("Couldn't reach the controller", true);
  }
  showNow();
});

showNow();
