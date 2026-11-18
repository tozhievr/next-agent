const {
  app,
  BrowserWindow,
  ipcMain,
  dialog,
  protocol,
  net,
  safeStorage,
} = require("electron");
const { Worker } = require("node:worker_threads");
const path = require("node:path");
const { pathToFileURL } = require("node:url");
// Portable launches keep their profile beside the executable/AppImage.
const setupBundle = require("node:fs").existsSync(
  path.join(process.resourcesPath, "payload-manifest.json"),
);
const portableRoot =
  process.env.PORTABLE_EXECUTABLE_DIR ||
  (process.env.APPIMAGE ? path.dirname(process.env.APPIMAGE) : "");
if (portableRoot && !setupBundle && !app.commandLine.hasSwitch("user-data-dir"))
  app.setPath("userData", path.join(portableRoot, "NextAgent Harness Data"));
protocol.registerSchemesAsPrivileged([
  {
    scheme: "harness",
    privileges: { standard: true, secure: true, supportFetchAPI: true },
  },
]);
const dev = process.env.NEXTAGENT_DEV_URL;
let win,
  worker,
  sequence = 0,
  root = "",
  model = "openrouter/auto",
  key = process.env.OPENROUTER_API_KEY || "",
  saved = {};
const pending = new Map();
function request(request) {
  return new Promise((resolve, reject) => {
    const id = ++sequence;
    pending.set(id, { resolve, reject });
    worker.postMessage({ id, request });
  });
}
async function persist() {
  await request({ action: "preferencesWrite", text: JSON.stringify(saved) });
}
function canEncrypt() {
  return (
    safeStorage.isEncryptionAvailable() &&
    (!safeStorage.getSelectedStorageBackend ||
      safeStorage.getSelectedStorageBackend() !== "basic_text")
  );
}
function publicSettings() {
  return { model, hasKey: !!key, canStoreKey: canEncrypt() };
}
let modelCatalog = null;
function invalidateModels() {
  modelCatalog = null;
}
async function listModels() {
  if (modelCatalog) return modelCatalog;
  const headers = { Accept: "application/json" };
  if (key) headers.Authorization = "Bearer " + key;
  const response = await net.fetch("https://openrouter.ai/api/v1/models", {
    headers,
  });
  if (!response.ok)
    throw new Error("OpenRouter не отдал каталог моделей: " + response.status);
  const body = await response.json();
  const rows = Array.isArray(body.data) ? body.data : [];
  const models = rows
    .filter((item) => item && typeof item.id === "string" && item.id.length < 200)
    .map((item) => ({
      id: item.id,
      name: typeof item.name === "string" && item.name ? item.name : item.id,
      context: Number(item.context_length) || 0,
      tools:
        Array.isArray(item.supported_parameters) &&
        item.supported_parameters.includes("tools"),
    }));
  if (!models.some((item) => item.id === "openrouter/auto"))
    models.unshift({
      id: "openrouter/auto",
      name: "OpenRouter Auto",
      context: 0,
      tools: true,
    });
  models.sort((a, b) => {
    if (a.id === "openrouter/auto") return -1;
    if (b.id === "openrouter/auto") return 1;
    if (a.tools !== b.tools) return a.tools ? -1 : 1;
    return a.name.localeCompare(b.name);
  });
  modelCatalog = models;
  return models;
}
app
  .whenReady()
  .then(async () => {
    const resources = app.isPackaged
      ? process.resourcesPath
      : path.join(__dirname, "..");
    if (process.argv.includes("--uninstall")) {
      const fs = require("original-fs").promises;
      const target = path.dirname(process.execPath);
      const marker = JSON.parse(
        await fs.readFile(path.join(target, ".nextagent-install.json"), "utf8"),
      );
      if (marker.appId !== "dev.nextagent.harness")
        throw new Error("Not an installed application");
      const temporary = await fs.mkdtemp(
        path.join(app.getPath("temp"), "nextagent-uninstall-"),
      );
      const runtime = path.join(temporary, "runtime");
      await fs.cp(target, runtime, { recursive: true });
      require("node:child_process")
        .spawn(
          path.join(runtime, path.basename(process.execPath)),
          ["--uninstall-target", target],
          { detached: true, stdio: "ignore" },
        )
        .unref();
      app.quit();
      return;
    }
    const uninstallIndex = process.argv.indexOf("--uninstall-target");
    if (uninstallIndex >= 0) {
      await require("./setup.cjs").run(
        resources,
        process.argv[uninstallIndex + 1],
      );
      return;
    }
    if (
      require("node:fs").existsSync(
        path.join(resources, "payload-manifest.json"),
      )
    ) {
      await require("./setup.cjs").run(resources);
      return;
    }
    const nativeDir = path.join(
      resources,
      app.isPackaged
        ? "native"
        : `native/bin/${process.platform === "win32" ? "win" : process.platform === "darwin" ? "mac" : "linux"}`,
    );
    process.env.NEXTAGENT_PLUGIN_DIR = path.join(nativeDir, "plugins");
    worker = new Worker(path.join(__dirname, "worker.cjs"), {
      workerData: { addon: path.join(nativeDir, "nextagent_desktop.node") },
    });
    worker.on("message", ({ id, result, error }) => {
      const p = pending.get(id);
      if (!p) return;
      pending.delete(id);
      if (error) p.reject(new Error(error));
      else p.resolve(result);
    });
    worker.on("exit", () => {
      for (const p of pending.values())
        p.reject(new Error("Native worker stopped"));
      pending.clear();
    });
    worker.on("error", (error) => {
      for (const p of pending.values()) p.reject(error);
      pending.clear();
    });
    await request({
      action: "init",
      state: path.join(app.getPath("userData"), "state"),
    });
    try {
      saved = JSON.parse(
        (await request({ action: "preferencesRead" })).text || "{}",
      );
      model = saved.model || model;
      if (!key && saved.key && canEncrypt())
        key = safeStorage.decryptString(Buffer.from(saved.key, "base64"));
    } catch {
      saved = {};
    }
    if (saved.root)
      try {
        await request({ action: "open", root: saved.root });
        root = saved.root;
      } catch {
        root = "";
      }
    protocol.handle("harness", (request) => {
      const url = new URL(request.url);
      if (url.host !== "app") return new Response("Forbidden", { status: 403 });
      let name = decodeURIComponent(url.pathname);
      if (name === "/" || name === "/index.html") name = "/index.html";
      const base = path.resolve(resources, "out"),
        file = path.resolve(base, "." + name);
      if (!file.startsWith(base + path.sep))
        return new Response("Forbidden", { status: 403 });
      return net.fetch(pathToFileURL(file).href).then((response) => {
        const headers = new Headers(response.headers);
        headers.set(
          "Content-Security-Policy",
          "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; object-src 'none'; frame-src 'none'",
        );
        return new Response(response.body, {
          status: response.status,
          headers,
        });
      });
    });
    ipcMain.handle("harness:request", async (event, input) => {
      const url = event.senderFrame?.url || "";
      if (
        event.sender !== win.webContents ||
        event.senderFrame !== win.webContents.mainFrame ||
        !(
          url.startsWith("harness://app/") ||
          (dev && new URL(url).origin === new URL(dev).origin)
        )
      )
        throw new Error("Untrusted frame");
      if (!input || typeof input.action !== "string")
        throw new Error("Invalid request");
      const { action } = input;
      if (action === "bootstrap")
        return {
          root,
          settings: publicSettings(),
          session: await request({ action: "snapshot" }),
          sessions: await request({ action: "list" }),
          capabilities: root ? await request({ action: "capabilities" }) : null,
        };
      if (action === "chooseWorkspace") {
        const result = await dialog.showOpenDialog(win, {
          properties: ["openDirectory"],
        });
        if (result.canceled) return null;
        const opened = await request({
          action: "open",
          root: result.filePaths[0],
        });
        root = opened.root;
        saved.root = root;
        await persist();
        return opened;
      }
      if (action === "settings") {
        const v = input.values;
        if (!v || typeof v.model !== "string" || v.model.length > 200)
          throw new Error("Invalid model");
        model = v.model.trim() || "openrouter/auto";
        saved.model = model;
        if (typeof v.apiKey === "string" && v.apiKey.trim()) {
          invalidateModels();
          const nextKey = v.apiKey.trim();
          if (/[\r\n]/.test(nextKey) || nextKey.length > 2048)
            throw new Error("Invalid API key");
          key = nextKey;
          saved.key = canEncrypt()
            ? safeStorage.encryptString(key).toString("base64")
            : undefined;
        }
        if (v.clearKey) {
          invalidateModels();
          key = "";
          delete saved.key;
        }
        await persist();
        return publicSettings();
      }
      if (action === "models") return listModels();
      if (action === "window") {
        if (input.command === "minimize") win.minimize();
        else if (input.command === "maximize") {
          if (win.isMaximized()) win.unmaximize();
          else win.maximize();
        } else if (input.command === "close") win.close();
        return null;
      }
      if (action === "selfTest") {
        const testRoot = path.join(
          app.getPath("temp"),
          "nextagent-electron-self-test",
        );
        const opened = await request({ action: "open", root: testRoot });
        root = opened.root;
        await request({
          action: "send",
          prompt: "Проверь нативный цикл Harness",
          model: "offline-self-test",
          offline: true,
        });
        return opened;
      }
      if (action === "send") {
        if (typeof input.prompt !== "string") throw new Error("Invalid prompt");
        return request({
          action: "send",
          prompt: input.prompt,
          readOnly: !!input.readOnly,
          model,
          key,
        });
      }
      const allowed = new Set([
        "new",
        "restore",
        "snapshot",
        "list",
        "tree",
        "read",
        "history",
        "labels",
      ]);
      if (action === "cancel") return request({ action });
      if (!allowed.has(action)) throw new Error("Unsupported operation");
      if (
        ["read", "tree"].includes(action) &&
        input.path !== undefined &&
        typeof input.path !== "string"
      )
        throw new Error("Invalid path");
      return request({ action, path: input.path || "", id: input.id });
    });
    win = new BrowserWindow({
      width: 1500,
      height: 960,
      minWidth: 1000,
      minHeight: 680,
      frame: false,
      icon: path.join(
        resources,
        app.isPackaged ? "assets/icon.png" : "assets/icon.png",
      ),
      backgroundColor: "#0a0a0a",
      show: false,
      webPreferences: {
        preload: path.join(__dirname, "preload.cjs"),
        contextIsolation: true,
        nodeIntegration: false,
        sandbox: true,
      },
    });
    win.webContents.setWindowOpenHandler(() => ({ action: "deny" }));
    win.webContents.on("will-navigate", (e, url) => {
      if (url !== win.webContents.getURL()) e.preventDefault();
    });
    win.webContents.session.setPermissionRequestHandler((_w, _p, callback) =>
      callback(false),
    );
    win.once("ready-to-show", () => win.show());
    await win.loadURL(dev || "harness://app/");
  })
  .catch((error) => {
    dialog.showErrorBox("NextAgent Harness", error.stack || error.message);
    app.exit(1);
  });
let quitting = false;
app.on("before-quit", (event) => {
  if (worker && !quitting) {
    event.preventDefault();
    quitting = true;
    request({ action: "shutdown" }).finally(() =>
      worker.terminate().finally(() => app.quit()),
    );
  }
});
app.on("window-all-closed", () => app.quit());
