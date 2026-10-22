// The library: pieces saved on the controller. Tapping one loads it; its "⋯"
// menu renames, downloads or deletes it. Uses ui (/assets/app.js), and
// showBusy(), closeLibrary() and the playback state from app.js.
const piecesList = document.getElementById("pieces");
const librarySearch = document.getElementById("library-search");
const librarySort = document.getElementById("library-sort");
const libraryUsage = document.getElementById("library-usage");
const libraryEmpty = document.getElementById("library-empty");
let libraryPieces = [];
let libraryLoaded = false;
let loadedLibraryId = "";
let renaming = null; // the id being renamed: the list isn't redrawn under it

try {
  librarySort.value = localStorage.getItem("library-sort") || "played";
} catch { /* not available */ }

// Skeleton rows until the first list arrives.
piecesList.replaceChildren(...[0, 1, 2].map(() => ui.el("li", { class: "skeleton-row" },
  ui.el("div", { class: "skeleton" }), ui.el("div", { class: "lines" }, ui.el("div", { class: "skeleton" }), ui.el("div", { class: "skeleton" })))));

async function loadLibrary() {
  let body;
  try {
    body = await ui.api("/api/library");
  } catch {
    if (!libraryLoaded) {
      piecesList.replaceChildren();
      libraryEmpty.replaceChildren(ui.el("div", { class: "empty", html: ui.icon("wifi") },
        ui.el("h3", {}, "Couldn't load the library"), ui.el("button", { class: "btn btn-sm", type: "button",
          onclick: loadLibrary }, "Try again")));
    }
    return;
  }
  libraryLoaded = true;
  libraryPieces = body.pieces;
  const mb = (bytes) => (bytes / 1048576).toFixed(1);
  libraryUsage.textContent = body.available
    ? `${libraryPieces.length} ${libraryPieces.length === 1 ? "piece" : "pieces"} · ${mb(body.usedBytes)} of ${mb(body.totalBytes)} MB used`
    : "Not available: upload the firmware with the new partition table";
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

const size = (bytes) => (bytes < 1048576 ? `${Math.ceil(bytes / 1024)} KB` : `${(bytes / 1048576).toFixed(1)} MB`);

function drawLibrary() {
  if (renaming || !libraryLoaded) return;
  const words = librarySearch.value.toLowerCase().split(/\s+/).filter(Boolean);
  const shown = libraryPieces
    .filter((p) => words.every((w) => p.name.toLowerCase().includes(w)))
    .sort({
      played: (a, b) => b.lastPlayed - a.lastPlayed || b.added - a.added || a.name.localeCompare(b.name),
      added: (a, b) => b.added - a.added || a.name.localeCompare(b.name),
      name: (a, b) => a.name.localeCompare(b.name, undefined, { numeric: true, sensitivity: "base" }),
    }[librarySort.value]);
  piecesList.replaceChildren(...shown.map(pieceRow));
  if (!libraryPieces.length) {
    libraryEmpty.replaceChildren(ui.el("div", { class: "empty", html: ui.icon("library") },
      ui.el("h3", {}, "Your library is empty"),
      ui.el("p", { class: "small" }, "Pieces you add are kept here, to play again without uploading them."),
      ui.el("label", { class: "btn btn-primary", for: "file", html: ui.icon("plus") }, "Add a piece")));
  } else if (!shown.length) {
    libraryEmpty.replaceChildren(ui.el("div", { class: "empty" }, ui.el("p", {}, `No piece matches “${librarySearch.value}”.`)));
  } else {
    libraryEmpty.replaceChildren();
  }
}

function pieceRow(piece) {
  const loaded = piece.id === loadedLibraryId;
  // The full name on hover, in case it's cut short.
  const name = ui.el("div", { class: "piece-name", title: piece.name }, piece.name);
  const meta = ui.el("div", { class: "piece-meta" }, [ui.formatTime(piece.durationMs), `${piece.notes.toLocaleString()} notes`,
    size(piece.bytes), piece.lastPlayed ? `played ${when(piece.lastPlayed)}` : piece.added ? `added ${when(piece.added)}` : ""]
    .filter(Boolean).join(" · "));
  const more = ui.el("button", { class: "btn btn-ghost btn-icon btn-sm", type: "button", "aria-label": `More for ${piece.name}`,
    html: ui.icon("more") });
  const li = ui.el("li", { class: (loaded ? "loaded" : "") + (libraryBusy ? " disabled" : ""), tabIndex: 0,
    "aria-label": `${piece.name}${loaded ? ", loaded" : ""}` },
    ui.el("div", { class: "piece-icon", html: ui.icon(loaded && playing ? "pause" : loaded ? "play" : "music") }),
    ui.el("div", { class: "piece-text" }, name, meta), more);
  const choose = () => {
    if (renaming) return;
    if (libraryBusy) {
      ui.toast("Stop the piece that's playing first");
      return;
    }
    if (loaded) {
      closeLibrary();
      return;
    }
    loadFromLibrary(piece);
  };
  li.addEventListener("click", (event) => {
    if (!event.target.closest("button, input")) choose();
  });
  li.addEventListener("keydown", (event) => {
    if (event.key === "Enter" && event.target === li) choose();
  });
  more.addEventListener("click", (event) => {
    event.stopPropagation();
    ui.menu(more, [
      { label: "Rename", icon: "edit", onclick: () => rename(piece, name) },
      { label: "Download", icon: "download", href: "/api/library/file?id=" + piece.id, download: piece.name + ".mid" },
      { label: "Delete", icon: "trash", danger: true, onclick: () => remove(piece) },
    ]);
  });
  return li;
}

async function remove(piece) {
  const sure = await ui.confirm({
    title: `Delete “${piece.name}”?`,
    message: "It's removed from the library with its settings. This can't be undone (a backup can bring it back).",
    confirm: "Delete", danger: true,
  });
  if (!sure) return;
  if (await ui.request("/api/library/delete", { json: { id: piece.id } })) {
    ui.toast(`Deleted “${piece.name}”`);
  }
  loadLibrary();
}

function rename(piece, nameEl) {
  if (renaming) return;
  renaming = piece.id;
  const input = ui.el("input", { class: "input input-sm", value: piece.name, maxLength: 80, "aria-label": "Name" });
  nameEl.replaceChildren(input);
  input.focus();
  input.select();
  let done = false;
  const finish = async (save) => {
    if (done) return;
    done = true;
    const name = input.value.trim();
    if (save && name && name !== piece.name) {
      await ui.request("/api/library/rename", { json: { id: piece.id, name } });
    }
    renaming = null;
    loadLibrary();
  };
  input.addEventListener("click", (event) => event.stopPropagation());
  input.addEventListener("keydown", (event) => {
    event.stopPropagation();
    if (event.key === "Enter") finish(true);
    if (event.key === "Escape") finish(false);
  });
  input.addEventListener("blur", () => finish(true));
}

async function loadFromLibrary(piece) {
  uploading = true; // the same indicator: it's built like an upload
  showBusy();
  closeLibrary();
  const summary = await ui.request("/api/library/load", { json: { id: piece.id } });
  uploading = false;
  showBusy();
  if (summary && summary.mergedNotes > 0) {
    ui.toast(`${summary.mergedNotes} repeats too fast to strike again are held instead`);
  }
  ui.refreshStatus();
  loadLibrary();
}

librarySearch.addEventListener("input", drawLibrary);
librarySort.addEventListener("change", () => {
  try { localStorage.setItem("library-sort", librarySort.value); } catch { /* not available */ }
  drawLibrary();
});
