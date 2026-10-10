const fileInput = document.getElementById("file");
const piece = document.getElementById("piece");
const playButton = document.getElementById("play");
const stopButton = document.getElementById("stop");
const progress = document.getElementById("progress");
const time = document.getElementById("time");
const message = document.getElementById("message");

function formatTime(ms) {
  const seconds = Math.floor(ms / 1000);
  return Math.floor(seconds / 60) + ":" + String(seconds % 60).padStart(2, "0");
}

async function request(url, options) {
  try {
    const response = await fetch(url, options);
    const body = await response.json();
    message.textContent = response.ok ? "" : body.error;
    return response.ok ? body : null;
  } catch (error) {
    message.textContent = "request failed: " + error.message;
    return null;
  }
}

fileInput.addEventListener("change", async () => {
  const file = fileInput.files[0];
  if (!file) {
    return;
  }
  message.textContent = "Uploading...";
  const summary = await request("/api/midi?name=" + encodeURIComponent(file.name), {
    method: "POST",
    headers: { "Content-Type": "application/octet-stream" },
    body: file
  });
  if (summary) {
    const notices = [];
    if (summary.skippedNotes > 0) {
      notices.push(summary.skippedNotes + " notes outside the 88 keys were skipped");
    }
    if (summary.mergedNotes > 0) {
      notices.push(summary.mergedNotes + " repeated notes were too fast to re-strike and were held instead");
    }
    message.textContent = notices.join("; ");
  }
  fileInput.value = "";
  refresh();
});

playButton.addEventListener("click", async () => {
  await request("/api/playback/play", { method: "POST" });
  refresh();
});

stopButton.addEventListener("click", async () => {
  await request("/api/playback/stop", { method: "POST" });
  refresh();
});

async function refresh() {
  let status;
  try {
    status = await (await fetch("/api/status")).json();
  } catch (error) {
    return;
  }
  const playback = status.playback;
  const playing = playback.state === "playing";

  if (playback.piece) {
    piece.textContent = playback.piece.title + " (" + playback.piece.notes + " notes, " +
      formatTime(playback.piece.durationMs) + ")";
    progress.max = Math.max(playback.piece.durationMs, 1);
    progress.value = playing ? Math.min(playback.positionMs, playback.piece.durationMs) : 0;
    time.textContent = formatTime(playing ? playback.positionMs : 0) + " / " + formatTime(playback.piece.durationMs);
  } else {
    piece.textContent = "No piece loaded";
    time.textContent = "";
  }

  fileInput.disabled = playing;
  playButton.disabled = playing || !playback.piece;
  stopButton.disabled = !playing;
}

refresh();
setInterval(refresh, 500);
