const { test } = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs/promises");
const path = require("node:path");
const os = require("node:os");
const crypto = require("node:crypto");
const { install, uninstall } = require("../electron/install-engine.cjs");
test("installer verifies payload, preserves existing folders and rolls back corrupt/cancelled installs", async () => {
  const base = await fs.mkdtemp(
      path.join(os.tmpdir(), "nextagent-setup-test-"),
    ),
    payload = path.join(base, "payload");
  await fs.mkdir(payload);
  const bytes = Buffer.alloc(1024 * 1024, 7);
  await fs.writeFile(path.join(payload, "application.bin"), bytes);
  const manifest = {
    version: "test",
    files: [
      {
        path: "application.bin",
        size: bytes.length,
        mode: 0o755,
        sha256: crypto.createHash("sha256").update(bytes).digest("hex"),
      },
    ],
  };
  const target = path.join(base, "app");
  const progress = [];
  await install({
    payload,
    manifest,
    target,
    onProgress: (p) => progress.push(p),
  });
  assert.equal(
    (await fs.readFile(path.join(target, "application.bin"))).length,
    bytes.length,
  );
  assert.equal(progress.at(-1).phase, "complete");
  const unmarked = path.join(base, "user-project");
  await fs.mkdir(unmarked);
  await fs.writeFile(path.join(unmarked, "user.txt"), "preserve");
  await assert.rejects(install({ payload, manifest, target: unmarked }));
  assert.equal(
    await fs.readFile(path.join(unmarked, "user.txt"), "utf8"),
    "preserve",
  );
  const bad = structuredClone(manifest);
  bad.files[0].sha256 = "bad";
  await assert.rejects(install({ payload, manifest: bad, target }));
  assert.equal(
    (await fs.readFile(path.join(target, "application.bin"))).length,
    bytes.length,
  );
  const control = new AbortController();
  control.abort();
  await assert.rejects(
    install({
      payload,
      manifest,
      target: path.join(base, "cancelled"),
      signal: control.signal,
    }),
  );
  assert.equal(
    await fs.stat(path.join(base, "cancelled")).then(
      () => true,
      () => false,
    ),
    false,
  );
  const escape = structuredClone(manifest);
  escape.files[0].path = "../escape";
  await assert.rejects(
    install({
      payload,
      manifest: escape,
      target: path.join(base, "escape-target"),
    }),
  );
  await assert.rejects(uninstall(unmarked));
  await uninstall(target);
  assert.equal(
    await fs.stat(target).then(
      () => true,
      () => false,
    ),
    false,
  );
  assert.equal(
    (await fs.readdir(base)).some((n) => n.startsWith(".nextagent-stage-")),
    false,
  );
});
