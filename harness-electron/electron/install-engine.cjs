const rawFs = process.versions.electron
  ? require("original-fs")
  : require("node:fs");
const fs = rawFs.promises;
const { createReadStream, createWriteStream } = rawFs;
const path = require("node:path");
const crypto = require("node:crypto");
const { pipeline } = require("node:stream/promises");
const { Transform } = require("node:stream");
const APP_ID = "dev.nextagent.harness";
async function install({
  payload,
  manifest,
  target,
  onProgress = () => {},
  signal,
}) {
  target = path.resolve(target);
  const parent = path.dirname(target);
  if (target === parent)
    throw new Error("Choose an application folder, not a filesystem root");
  const token = crypto.randomUUID();
  const staging = path.join(parent, `.nextagent-stage-${token}`),
    backup = path.join(parent, `.nextagent-backup-${token}`);
  let moved = false,
    committed = false;
  async function ownedRemove(p) {
    if (
      path.dirname(p) !== parent ||
      !path.basename(p).startsWith(".nextagent-")
    )
      throw new Error("Unsafe cleanup path");
    await fs.rm(p, { recursive: true, force: true });
  }
  const entries = manifest.files;
  if (!Array.isArray(entries) || !entries.length)
    throw new Error("Missing payload manifest");
  try {
    const info = await fs.lstat(target);
    if (info.isSymbolicLink() || !info.isDirectory())
      throw new Error("Destination is not an application folder");
    const marker = JSON.parse(
      await fs.readFile(path.join(target, ".nextagent-install.json"), "utf8"),
    );
    if (marker.appId !== APP_ID)
      throw new Error("Destination belongs to another application");
  } catch (e) {
    if (e.code !== "ENOENT") throw e;
    const exists = await fs.stat(target).then(
      () => true,
      () => false,
    );
    if (exists)
      throw new Error(
        "Choose an empty destination; existing unmarked folders are protected",
      );
  }
  await fs.mkdir(parent, { recursive: true });
  await fs.mkdir(staging, { recursive: false });
  const total = entries.reduce((n, e) => n + e.size, 0);
  let bytes = 0,
    last = 0;
  try {
    for (const e of entries) {
      if (signal?.aborted) throw new Error("Installation cancelled");
      const src = path.resolve(payload, e.path),
        dest = path.resolve(staging, e.path);
      if (
        !src.startsWith(path.resolve(payload) + path.sep) ||
        !dest.startsWith(staging + path.sep)
      )
        throw new Error("Unsafe payload entry");
      const stat = await fs.lstat(src);
      if (!stat.isFile() || stat.isSymbolicLink() || stat.size !== e.size)
        throw new Error(`Invalid payload: ${e.path}`);
      await fs.mkdir(path.dirname(dest), { recursive: true });
      const hash = crypto.createHash("sha256");
      await pipeline(
        createReadStream(src),
        new Transform({
          transform(chunk, _encoding, cb) {
            hash.update(chunk);
            bytes += chunk.length;
            const now = Date.now();
            if (now - last > 50) {
              last = now;
              onProgress({ phase: "copy", bytes, total, file: e.path });
            }
            cb(null, chunk);
          },
        }),
        createWriteStream(dest, { flags: "wx", mode: e.mode }),
        { signal },
      );
      if (hash.digest("hex") !== e.sha256)
        throw new Error(`Checksum mismatch: ${e.path}`);
      await fs.chmod(dest, e.mode);
    }
    await fs.writeFile(
      path.join(staging, ".nextagent-install.json"),
      JSON.stringify({ appId: APP_ID, version: manifest.version }),
    );
    if (signal?.aborted) throw new Error("Installation cancelled");
    try {
      await fs.rename(target, backup);
      moved = true;
    } catch (e) {
      if (e.code !== "ENOENT") throw e;
    }
    try {
      await fs.rename(staging, target);
      committed = true;
    } catch (e) {
      if (moved) await fs.rename(backup, target);
      moved = false;
      throw e;
    }
    if (moved) await ownedRemove(backup);
    onProgress({ phase: "complete", bytes: total, total });
    return { target, version: manifest.version };
  } catch (e) {
    if (!committed) await ownedRemove(staging);
    throw e;
  }
}
async function uninstall(target, onProgress = () => {}) {
  target = path.resolve(target);
  if (target === path.dirname(target)) throw new Error("Unsafe uninstall root");
  const info = await fs.lstat(target);
  if (info.isSymbolicLink() || !info.isDirectory())
    throw new Error("Invalid application directory");
  const marker = JSON.parse(
    await fs.readFile(path.join(target, ".nextagent-install.json"), "utf8"),
  );
  if (marker.appId !== APP_ID)
    throw new Error("This directory is not a managed Harness installation");
  const actual = await fs.realpath(target);
  if (actual !== target && process.platform !== "win32")
    throw new Error("Refusing redirected uninstall target");
  await fs.rm(target, { recursive: true, force: false });
  onProgress({ phase: "complete", bytes: 1, total: 1 });
}
module.exports = { install, uninstall, APP_ID };
