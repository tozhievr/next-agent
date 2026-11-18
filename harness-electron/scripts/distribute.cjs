const fs = require("node:fs/promises");
const path = require("node:path");
const crypto = require("node:crypto");
const { build, Platform, Arch } = require("electron-builder");
const project = path.resolve(__dirname, "..");
async function configuredBuild(options) {
  const directory = path.join(project, "release", "configs");
  await fs.mkdir(directory, { recursive: true });
  const file = path.join(directory, crypto.randomUUID() + ".json");
  await fs.writeFile(file, JSON.stringify(options.config));
  return build({ ...options, config: file, publish: "never" });
}
async function manifest(root, prefix = "") {
  const entries = [];
  for (const name of (await fs.readdir(path.join(root, prefix))).sort()) {
    const relative = path.posix.join(prefix, name);
    const full = path.join(root, relative);
    const info = await fs.lstat(full);
    if (info.isSymbolicLink())
      throw new Error(`Payload symlinks are not supported: ${relative}`);
    if (info.isDirectory()) entries.push(...(await manifest(root, relative)));
    else if (info.isFile())
      entries.push({
        path: relative,
        size: info.size,
        mode: info.mode & 0o777,
        sha256: crypto
          .createHash("sha256")
          .update(await fs.readFile(full))
          .digest("hex"),
      });
  }
  return entries;
}
(async () => {
  const pkg = require("../package.json"),
    windows = process.platform === "win32",
    mac = process.platform === "darwin";
  const config = {
    ...pkg.build,
    electronDist: process.env.NEXTAGENT_ELECTRON_DIST || pkg.build.electronDist,
  };
  const platform = windows ? Platform.WINDOWS : mac ? Platform.MAC : Platform.LINUX;
  const arch = process.arch === "arm64" ? Arch.arm64 : Arch.x64;
  const output = path.join(project, "release", windows ? "windows" : mac ? "macos" : "linux");
  config.directories = { output: path.relative(project, output) };
  await configuredBuild({
    projectDir: project,
    targets: platform.createTarget(windows ? ["portable"] : mac ? ["dmg"] : ["AppImage"], arch),
    config: {
      ...config,
      artifactName: "NextAgent-Harness-${version}-${os}-${arch}-Portable.${ext}",
      ...(windows
        ? {
            portable: {
              artifactName: "NextAgent-Harness-${version}-windows-${arch}-Portable.exe",
            },
          }
        : mac
          ? { mac: { ...config.mac, identity: null } }
          : {
              deb: {
                artifactName: "NextAgent-Harness-${version}-linux-${arch}.deb",
              },
            }),
    },
  });
  const unpackedName = windows ? "win-unpacked" : mac ? (process.arch === "arm64" ? "mac-arm64" : "mac") : "linux-unpacked";
  const payload = path.join(output, unpackedName);
  const description = { version: pkg.version, files: await manifest(payload) };
  const manifestPath = path.join(output, "payload-manifest.json");
  await fs.writeFile(manifestPath, JSON.stringify(description));
  await configuredBuild({
    projectDir: project,
    targets: platform.createTarget(windows ? ["portable"] : mac ? ["dmg"] : ["AppImage"], arch),
    config: {
      ...config,
      appId: "dev.nextagent.harness.setup",
      productName: "NextAgent Harness Setup",
      extraMetadata: { name: "nextagent-harness-setup" },
      directories: {
        output: path.relative(project, path.join(output, "setup")),
      },
      artifactName: "NextAgent-Harness-${version}-${os}-${arch}-Setup.${ext}",
      extraResources: [
        { from: "out", to: "out" },
        { from: "assets", to: "assets" },
        { from: payload, to: "payload" },
        { from: manifestPath, to: "payload-manifest.json" },
      ],
      ...(windows
        ? {
            portable: {
              artifactName: "NextAgent-Harness-${version}-windows-${arch}-Setup.exe",
            },
          }
        : mac
          ? { mac: { ...config.mac, identity: null } }
          : {}),
    },
  });
  const resultFiles = [];
  for (const directory of [output, path.join(output, "setup")])
    for (const name of await fs.readdir(directory)) {
      if (/\.(exe|AppImage|deb|dmg)$/.test(name)) {
        const file = path.join(directory, name);
        const hash = crypto
          .createHash("sha256")
          .update(await fs.readFile(file))
          .digest("hex");
        await fs.writeFile(file + ".sha256", `${hash}  ${name}\n`);
        resultFiles.push(file);
      }
    }
  console.log("Harness distributions:\n" + resultFiles.join("\n"));
})().catch((e) => {
  console.error(e);
  process.exitCode = 1;
});
