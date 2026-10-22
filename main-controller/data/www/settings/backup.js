// Backs up what the piano keeps (the playing rules, key offsets, the library)
// as a zip made here in the browser, and restores one through the same API
// the other pages use. settings.json holds the settings and the library's
// list (each piece's "file" in the zip); pieces/<name>.mid each piece's file.
(() => {
const $ = (id) => document.getElementById(id);
const FORMAT = "player-piano-backup";
const mb = (bytes) => (bytes < 1048576 ? Math.ceil(bytes / 1024) + " KB" : (bytes / 1048576).toFixed(1) + " MB");

// A piece's file name in the zip: its name, made safe for any computer, and
// numbered if two pieces have the same one.
function fileNames(pieces) {
  const used = new Set();
  return pieces.map((piece) => {
    const base = piece.name.replace(/[\\/:*?"<>|\x00-\x1f]/g, "_").replace(/^[\s.]+|[\s.]+$/g, "").slice(0, 100) || "piece";
    let name = base, n = 2;
    while (used.has(name.toLowerCase())) name = `${base} (${n++})`;
    used.add(name.toLowerCase());
    return `pieces/${name}.mid`;
  });
}

const getJson = (url) => ui.api(url);
const postJson = (url, body) => ui.api(url, { json: body });

// A busy line (spinner and text) shown while text is set.
function say(element, text) {
  element.hidden = !text;
  element.querySelector("span:last-child").textContent = text;
}

// ---- back up ----
async function showLibrarySize() {
  try {
    const library = await getJson("/api/library");
    const bytes = library.pieces.reduce((n, p) => n + p.bytes, 0);
    $("pieces-size").textContent = `(${library.pieces.length} pieces, ${mb(bytes)})`;
  } catch {
    $("pieces-size").textContent = "";
  }
}

$("backup-button").addEventListener("click", async () => {
  const status = $("backup-status");
  $("backup-button").disabled = true;
  try {
    say(status, "Reading the settings…");
    const [config, offsets, library] = await Promise.all([
      getJson("/api/config"), getJson("/api/offsets"), getJson("/api/library")]);
    const names = fileNames(library.pieces);
    const settings = {
      format: FORMAT,
      version: 1,
      made: new Date().toISOString(),
      config: config.config,
      offsets: { keys: offsets.keys },
      library: library.pieces.map((piece, i) => ({ ...piece, file: names[i] })),
    };
    const files = [{ name: "settings.json", data: new TextEncoder().encode(JSON.stringify(settings, null, 1)) }];
    if ($("with-pieces").checked) {
      for (const [i, piece] of library.pieces.entries()) {
        say(status, `Reading piece ${i + 1} of ${library.pieces.length}: ${piece.name}`);
        const response = await fetch("/api/library/file?id=" + piece.id);
        if (!response.ok) throw new Error(`couldn't read “${piece.name}”`);
        files.push({ name: names[i], data: new Uint8Array(await response.arrayBuffer()) });
      }
    }
    const blob = zip.write(files);
    const link = document.createElement("a");
    link.href = URL.createObjectURL(blob);
    const today = new Date();
    const day = [today.getFullYear(), today.getMonth() + 1, today.getDate()].map((n) => String(n).padStart(2, "0"));
    link.download = `piano-backup-${day.join("-")}.zip`;
    document.body.append(link);
    link.click();
    link.remove();
    setTimeout(() => URL.revokeObjectURL(link.href), 10000);
    say(status, "");
    ui.toast(`Saved ${link.download} (${mb(blob.size)})` +
      ($("with-pieces").checked ? `, with ${files.length - 1} pieces` : ", without the pieces"));
  } catch (error) {
    say(status, "");
    ui.toast("Backup failed: " + error.message, { error: true });
  }
  $("backup-button").disabled = false;
});

// ---- restore ----
let backup = null; // {settings, files}

$("restore-file").addEventListener("change", async () => {
  const file = $("restore-file").files[0];
  $("restore-choices").hidden = true;
  $("restore-log").textContent = "";
  backup = null;
  if (!file) return;
  const status = $("restore-status");
  try {
    const files = await zip.read(await file.arrayBuffer());
    if (!files["settings.json"]) throw new Error("it has no settings.json");
    const settings = JSON.parse(new TextDecoder().decode(files["settings.json"]));
    if (settings.format !== FORMAT) throw new Error("it isn't a backup from this page");
    backup = { settings, files };
  } catch (error) {
    ui.toast(`Can't restore ${file.name}: ${error.message}`, { error: true });
    $("restore-file").value = "";
    return;
  }
  const s = backup.settings;
  const keys = Object.keys(s.offsets?.keys || {}).length;
  const pieces = (s.library || []).filter((p) => backup.files[pieceFile(p)]);
  $("restore-made").textContent = `${file.name}, made ${new Date(s.made).toLocaleString()}. Choose what to restore:`;
  $("restore-offsets-hint").textContent = keys
    ? `${keys} ${keys === 1 ? "key" : "keys"}: replaces the current ones`
    : "none in this backup: restoring clears the current ones";
  $("restore-library-hint").textContent = pieces.length
    ? `${pieces.length} pieces, added (one already there gets the backup's name and settings)`
    : "no pieces in this backup";
  $("restore-library").checked = pieces.length > 0;
  $("restore-library").disabled = pieces.length === 0;
  say(status, "");
  $("restore-choices").hidden = false;
});

// Where a piece's file is in the zip (older backups named them by id).
const pieceFile = (piece) => piece.file || `pieces/${piece.id}.mid`;

function note(line) {
  $("restore-log").textContent += line + "\n";
  $("restore-log").scrollTop = $("restore-log").scrollHeight;
}

$("restore-button").addEventListener("click", async () => {
  if (!backup) return;
  const s = backup.settings;
  const status = $("restore-status");
  $("restore-button").disabled = true;
  $("restore-log").textContent = "";
  let problems = 0;
  const step = async (label, action) => {
    try {
      const result = await action();
      note(`${label}: ${typeof result === "string" ? result : "done"}`);
    } catch (error) {
      problems++;
      note(`${label}: FAILED (${error.message})`);
    }
  };
  if ($("restore-settings").checked) {
    await step("Config", () => postJson("/api/config", s.config));
  }
  if ($("restore-offsets").checked) {
    await step("Key offsets", () => postJson("/api/offsets", { keys: s.offsets?.keys || {} }));
  }
  if ($("restore-library").checked) {
    const pieces = (s.library || []).filter((p) => backup.files[pieceFile(p)]);
    for (const [i, piece] of pieces.entries()) {
      say(status, `Piece ${i + 1} of ${pieces.length}…`);
      await step(`“${piece.name}”`, async () => {
        const response = await fetch("/api/library/add?name=" + encodeURIComponent(piece.name), {
          method: "POST",
          headers: { "Content-Type": "application/octet-stream" },
          body: backup.files[pieceFile(piece)],
        });
        const reply = await response.json().catch(() => ({}));
        if (!response.ok) throw new Error(reply.error || String(response.status));
        if (reply.library === "full") throw new Error("the library is full");
        await postJson("/api/library/details", { ...piece, id: reply.piece.id });
        return reply.library === "already" ? "already there: name and settings restored" : "added";
      });
    }
  }
  say(status, "");
  ui.toast(problems ? `Restored, with ${problems} ${problems === 1 ? "problem" : "problems"}: see the log below` : "Restored",
    { error: problems > 0 });
  $("restore-button").disabled = false;
  showLibrarySize();
});

showLibrarySize();
})();
