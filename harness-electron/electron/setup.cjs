const {
  BrowserWindow,
  ipcMain,
  dialog,
  protocol,
  net,
  shell,
  app,
} = require("electron");
const path = require("node:path");
const { pathToFileURL } = require("node:url");
const fs = require("node:fs/promises");
const { install, uninstall } = require("./install-engine.cjs");
const { execFile } = require("node:child_process");
const { promisify } = require("node:util");
const exec = promisify(execFile);
async function run(resources, uninstallTarget = "") {
  const payload = path.join(resources, "payload");
  const manifest = uninstallTarget
    ? { version: require("../package.json").version, files: [] }
    : JSON.parse(
        await fs.readFile(
          path.join(resources, "payload-manifest.json"),
          "utf8",
        ),
      );
  const executableName =
    process.platform === "win32"
      ? "NextAgent Harness.exe"
      : process.platform === "darwin"
        ? "NextAgent Harness.app"
        : "nextagent-harness";
  let target =
    process.platform === "win32"
      ? path.join(
          app.getPath("appData"),
          "..",
          "Local",
          "Programs",
          "NextAgent Harness",
        )
      : process.platform === "darwin"
        ? path.join(app.getPath("home"), "Applications", "NextAgent Harness")
        : path.join(app.getPath("home"), ".local", "opt", "nextagent-harness");
  if (process.env.NEXTAGENT_SETUP_TEST_TARGET)
    target = path.resolve(process.env.NEXTAGENT_SETUP_TEST_TARGET);
  if (uninstallTarget) target = path.resolve(uninstallTarget);
  let status = {
    mode: uninstallTarget ? "uninstall" : "install",
    phase: "welcome",
    bytes: 0,
    total: manifest.files.reduce((s, e) => s + e.size, 0),
    target,
    version: manifest.version,
    error: "",
  };
  let controller;
  protocol.handle("harness", (request) => {
    const base = path.resolve(resources, "out");
    let name = new URL(request.url).pathname;
    if (name === "/setup" || name === "/setup/") name = "/setup.html";
    const file = path.resolve(base, "." + decodeURIComponent(name));
    if (!file.startsWith(base + path.sep))
      return new Response("Forbidden", { status: 403 });
    return net.fetch(pathToFileURL(file).href).then((response) => {
      const headers = new Headers(response.headers);
      headers.set(
        "Content-Security-Policy",
        "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; object-src 'none'; frame-src 'none'",
      );
      return new Response(response.body, { status: response.status, headers });
    });
  });
  const win = new BrowserWindow({
    width: 860,
    height: 580,
    resizable: false,
    frame: false,
    backgroundColor: "#0a0a0a",
    icon: path.join(resources, "assets/icon.png"),
    webPreferences: {
      preload: path.join(__dirname, "setup-preload.cjs"),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
    },
  });
  win.webContents.setWindowOpenHandler(() => ({ action: "deny" }));
  win.webContents.on("will-navigate", (e) => e.preventDefault());
  win.webContents.session.setPermissionRequestHandler((_w, _p, cb) =>
    cb(false),
  );
  ipcMain.handle("setup:request", async (event, { action }) => {
    if (
      event.senderFrame !== win.webContents.mainFrame ||
      !event.senderFrame.url.startsWith("harness://app/")
    )
      throw new Error("Untrusted setup frame");
    if (action === "status") return status;
    if (action === "choose") {
      if (uninstallTarget)
        throw new Error("Uninstall target cannot be changed");
      if (status.phase === "copy") throw new Error("Installation is active");
      const selected = await dialog.showOpenDialog(win, {
        properties: ["openDirectory", "createDirectory"],
      });
      if (!selected.canceled) {
        target = path.join(selected.filePaths[0], "NextAgent Harness");
        status = { ...status, target };
      }
      return status;
    }
    if (action === "close") {
      if (controller) {
        if (uninstallTarget) return status;
        controller.abort();
        return status;
      }
      win.close();
      return status;
    }
    if (action === "launch") {
      if (status.phase !== "complete") throw new Error("Not installed");
      if (uninstallTarget) {
        win.close();
        return status;
      }
      await shell.openPath(path.join(target, executableName));
      win.close();
      return status;
    }
    if (action === "install") {
      if (controller || status.phase === "complete") return status;
      controller = new AbortController();
      status = { ...status, phase: "copy", error: "" };
      const operation = uninstallTarget
        ? uninstall(target, (update) => {
            status = { ...status, ...update };
          })
        : install({
            payload,
            manifest,
            target,
            signal: controller.signal,
            onProgress: (update) => {
              status = { ...status, ...update };
            },
          });
      operation
        .then(async () => {
          if (process.env.NEXTAGENT_SETUP_TEST_TARGET) {
            status = { ...status, phase: "complete" };
            return;
          }
          if (uninstallTarget) {
            if (process.platform === "win32") {
              for (const link of [
                path.join(app.getPath("desktop"), "NextAgent Harness.lnk"),
                path.join(
                  app.getPath("appData"),
                  "Microsoft",
                  "Windows",
                  "Start Menu",
                  "Programs",
                  "NextAgent Harness.lnk",
                ),
              ]) {
                try {
                  const info = shell.readShortcutLink(link);
                  if (
                    path.resolve(info.target).toLowerCase() ===
                    path.resolve(target, executableName).toLowerCase()
                  )
                    await fs.unlink(link);
                } catch {}
              }
              await exec("reg.exe", [
                "delete",
                "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\NextAgentHarness",
                "/f",
              ]).catch(() => {});
            } else if (process.platform !== "darwin")
              await fs.rm(
                path.join(
                  app.getPath("home"),
                  ".local/share/applications/dev.nextagent.harness.desktop",
                ),
                { force: true },
              );
            status = { ...status, phase: "complete" };
            return;
          }
          if (process.platform === "win32") {
            const executable = path.join(target, executableName);
            const registry =
              "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\NextAgentHarness";
            for (const [name, value] of Object.entries({
              DisplayName: "NextAgent Harness",
              DisplayVersion: manifest.version,
              Publisher: "NextAgent contributors",
              InstallLocation: target,
              DisplayIcon: executable,
              UninstallString: '"' + executable + '" --uninstall',
            }))
              await exec("reg.exe", [
                "add",
                registry,
                "/v",
                name,
                "/t",
                "REG_SZ",
                "/d",
                value,
                "/f",
              ]);

            const shortcut = {
              target: executable,
              cwd: target,
              description: "NextAgent Harness",
              icon: executable,
            };
            shell.writeShortcutLink(
              path.join(app.getPath("desktop"), "NextAgent Harness.lnk"),
              "create",
              shortcut,
            );
            const programs = path.join(
              app.getPath("appData"),
              "Microsoft",
              "Windows",
              "Start Menu",
              "Programs",
            );
            await fs.mkdir(programs, { recursive: true });
            shell.writeShortcutLink(
              path.join(programs, "NextAgent Harness.lnk"),
              "create",
              shortcut,
            );
          } else if (process.platform !== "darwin") {
            const directory = path.join(
              app.getPath("home"),
              ".local",
              "share",
              "applications",
            );
            await fs.mkdir(directory, { recursive: true });
            const escape = (v) => v.replace(/[\\"`$]/g, "\\$&");
            await fs.writeFile(
              path.join(directory, "dev.nextagent.harness.desktop"),
              `[Desktop Entry]\nType=Application\nName=NextAgent Harness\nExec="${escape(path.join(target, "nextagent-harness"))}"\nIcon=${path.join(target, "resources/assets/icon.png")}\nCategories=Development;\nTerminal=false\n`,
            );
          }
          status = { ...status, phase: "complete" };
        })
        .catch((e) => {
          status = { ...status, phase: "error", error: e.message };
        })
        .finally(() => {
          controller = null;
        });
      return status;
    }
    throw new Error("Unknown setup operation");
  });
  win.on("close", (event) => {
    if (controller) {
      event.preventDefault();
      controller.abort();
    }
  });
  await win.loadURL("harness://app/setup/");
}
module.exports = { run };
