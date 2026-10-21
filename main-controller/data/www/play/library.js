// The library: pieces saved on the controller, to load, rename, download or
// delete. Uses request(), message, showBusy() and the playback state from
// app.js.
const piecesList = document.getElementById("pieces");
const librarySearch = document.getElementById("library-search");
const librarySort = document.getElementById("library-sort");
const libraryUsage = document.getElementById("library-usage");
let libraryPieces = [];
let loadedLibraryId = "";
let renaming = null; // the id being renamed: the list isn't redrawn under it

try {
  librarySort.value = localStorage.getItem("library-sort") || "played";
} catch { /* not available */ }

async function loadLibrary() {
  let body = null;
  try {
    const response = await fetch("/api/library");
    body = response.ok ? await response.json() : null;
  } catch { /* shown as unavailable */ }
  if (!body) {
    libraryUsage.textContent = "";
    return;
  }
  libraryPieces = body.pieces;
  const mb = (bytes) => (bytes / 1048576).toFixed(1);
  libraryUsage.textContent = body.available
    ? `${libraryPieces.length} ${libraryPieces.length === 1 ? "piece" : "pieces"}, ${mb(body.usedBytes)} of ${mb(body.totalBytes)} MB used`
    : "not available: upload the firmware with the new partition table";
  drawLibrary();
}

function when(seconds) {
  if (!seconds) return "";
  const date = new Date(seconds * 1000), today = new Date();
  const days = Math.round((new Date(today.toDateString()) - new Date(date.toDateString())) / 86400000);
  if (days === 0) return "today";
  if (days === 1) return "yesterday";
  if (days < 7) return date.toLocaleDateString(undefined, { weekday: "long" });
  return date.toLocaleDateString(undefined, { day: "numeric", month: "short",
    ...(date.getFullYear() !== today.getFullYear() ? { year: "numeric" } : {}) });
}

function drawLibrary() {
  if (renaming) return;
  const words = librarySearch.value.toLowerCase().split(/\s+/).filter(Boolean);
  const shown = libraryPieces
    .filter((p) => words.every((w) => p.name.toLowerCase().includes(w)))
    .sort({
      played: (a, b) => b.lastPlayed - a.lastPlayed || b.added - a.added || a.name.localeCompare(b.name),
      added: (a, b) => b.added - a.added || a.name.localeCompare(b.name),
      name: (a, b) => a.name.localeCompare(b.name, undefined, { numeric: true, sensitivity: "base" }),
    }[librarySort.value]);
  const busy = libraryBusy;
  piecesList.replaceChildren(...shown.map((piece) => pieceRow(piece, busy)));
  const empty = document.getElementById("library-empty") || Object.assign(document.createElement("p"),
    { id: "library-empty", className: "library-empty" });
  empty.textContent = !libraryPieces.length ? "Pieces you add are saved here, to play again without uploading them."
    : !shown.length ? "No piece matches." : "";
  piecesList.after(empty);
  empty.hidden = !empty.textContent;
}

function pieceRow(piece, busy) {
  const li = document.createElement("li");
  li.classList.toggle("loaded", piece.id === loadedLibraryId);
  const text = document.createElement("div");
  text.className = "piece-text";
  const name = document.createElement("div");
  name.className = "piece-name";
  name.textContent = piece.name;
  name.title = "Click to rename";
  name.tabIndex = 0;
  name.addEventListener("click", () => rename(piece, name));
  name.addEventListener("keydown", (e) => { if (e.key === "Enter") rename(piece, name); });
  if (piece.id === loadedLibraryId) {
    const badge = document.createElement("span");
    badge.className = "piece-badge";
    badge.textContent = "loaded";
    name.append(badge);
  }
  const meta = document.createElement("div");
  meta.className = "piece-meta";
  meta.textContent = [formatTime(piece.durationMs), `${piece.notes.toLocaleString()} notes`,
    piece.bytes < 1048576 ? `${Math.ceil(piece.bytes / 1024)} KB` : `${(piece.bytes / 1048576).toFixed(1)} MB`,
    piece.added ? `added ${when(piece.added)}` : "",
    piece.lastPlayed ? `played ${when(piece.lastPlayed)}` : ""].filter(Boolean).join(" · ");
  text.append(name, meta);

  const load = document.createElement("button");
  load.type = "button";
  load.textContent = "Load";
  load.disabled = busy;
  load.addEventListener("click", () => loadFromLibrary(piece));
  const download = document.createElement("a");
  download.className = "button";
  download.href = "/api/library/file?id=" + piece.id;
  download.textContent = "Download";
  download.setAttribute("download", piece.name + ".mid");
  const remove = document.createElement("button");
  remove.type = "button";
  remove.textContent = "Delete";
  remove.addEventListener("click", async () => {
    if (!confirm(`Delete “${piece.name}” from the library?`)) return;
    await request("/api/library/delete", jsonPost({ id: piece.id }));
    loadLibrary();
  });
  li.append(text, load, download, remove);
  return li;
}

const jsonPost = (body) => ({ method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) });

function rename(piece, nameEl) {
  if (renaming) return;
  renaming = piece.id;
  const input = document.createElement("input");
  input.value = piece.name;
  input.maxLength = 80;
  input.setAttribute("aria-label", "Name");
  nameEl.replaceChildren(input);
  input.focus();
  input.select();
  let done = false;
  const finish = async (save) => {
    if (done) return;
    done = true;
    const name = input.value.trim();
    if (save && name && name !== piece.name) {
      await request("/api/library/rename", jsonPost({ id: piece.id, name }));
    }
    renaming = null;
    loadLibrary();
    refresh();
  };
  input.addEventListener("keydown", (e) => {
    if (e.key === "Enter") finish(true);
    if (e.key === "Escape") finish(false);
  });
  input.addEventListener("blur", () => finish(true));
}

async function loadFromLibrary(piece) {
  message.textContent = "";
  message.classList.remove("error");
  uploading = true; // the same indicator: it's built like an upload
  showBusy();
  const summary = await request("/api/library/load", jsonPost({ id: piece.id }));
  uploading = false;
  showBusy();
  if (summary && summary.mergedNotes > 0) {
    message.textContent = summary.mergedNotes + " repeated notes were too fast to re-strike and were held instead";
  }
  refresh();
  loadLibrary();
}

librarySearch.addEventListener("input", drawLibrary);
librarySort.addEventListener("change", () => {
  try { localStorage.setItem("library-sort", librarySort.value); } catch { /* not available */ }
  drawLibrary();
});
