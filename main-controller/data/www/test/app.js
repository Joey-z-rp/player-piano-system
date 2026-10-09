const form = document.getElementById("form");
const command = document.getElementById("command");
const log = document.getElementById("log");

form.addEventListener("submit", async (event) => {
  event.preventDefault();
  const text = command.value.trim();
  try {
    const response = await fetch("/api/driver/command", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ command: text })
    });
    const reply = await response.json();
    log.textContent += (response.ok ? "queued " + reply.queued : "error: " + reply.error) + "\n";
  } catch (error) {
    log.textContent += "request failed: " + error.message + "\n";
  }
  command.focus();
});
