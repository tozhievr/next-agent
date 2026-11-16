const { test } = require("node:test");
const assert = require("node:assert/strict");
const { Worker } = require("node:worker_threads");
const { mkdtempSync } = require("node:fs");
const os = require("node:os");
const path = require("node:path");
test("native workspace, streamed session, history and persistent restore", async () => {
  const base = mkdtempSync(
    path.join(os.tmpdir(), "nextagent-electron-native-"),
  );
  process.env.NEXTAGENT_PLUGIN_DIR = path.resolve(
    __dirname,
    `../native/bin/${process.platform === "win32" ? "win" : "linux"}/plugins`,
  );
  const worker = new Worker(path.resolve(__dirname, "../electron/worker.cjs"), {
    workerData: {
      addon: path.resolve(
        __dirname,
        `../native/bin/${process.platform === "win32" ? "win" : "linux"}/nextagent_desktop.node`,
      ),
    },
  });
  let seq = 0;
  const pending = new Map();
  worker.on("message", ({ id, result, error }) => {
    const p = pending.get(id);
    pending.delete(id);
    if (error) p.reject(new Error(error));
    else p.resolve(result);
  });
  const call = (request) =>
    new Promise((resolve, reject) => {
      const id = ++seq;
      pending.set(id, { resolve, reject });
      worker.postMessage({ id, request });
    });
  try {
    await call({ action: "init", state: path.join(base, "state") });
    const opened = await call({
      action: "open",
      root: path.join(base, "workspace"),
    });
    assert.equal(opened.capabilities.modules.length, 30);
    await call({
      action: "send",
      prompt: "Verify native Harness",
      model: "offline-self-test",
      offline: true,
    });
    let snapshot;
    for (let i = 0; i < 100; i++) {
      snapshot = await call({ action: "snapshot" });
      if (!snapshot.running && snapshot.messages.length) break;
      await new Promise((r) => setTimeout(r, 50));
    }
    assert.equal(snapshot.error, "");
    assert.equal(snapshot.running, false);
    assert.equal(snapshot.tool_calls, 1);
    assert.ok(snapshot.events.length);
    assert.deepEqual(snapshot.changed, ["harness-check.txt"]);
    assert.match(
      (await call({ action: "read", path: "harness-check.txt" })).text,
      /Native NextAgent transaction/,
    );
    assert.ok(
      (await call({ action: "tree" })).some(
        (e) => e.path === "harness-check.txt",
      ),
    );
    await assert.rejects(call({ action: "read", path: "../outside.txt" }));
    const list = await call({ action: "list" });
    assert.ok(list.some((s) => s.id === snapshot.id));
    await call({ action: "new" });
    const restored = await call({ action: "restore", id: snapshot.id });
    assert.equal(restored.messages.length, snapshot.messages.length);
    await assert.rejects(call({ action: "restore", id: "../bad" }));
  } finally {
    await call({ action: "shutdown" });
    await worker.terminate();
  }
});
