const { spawn } = require("node:child_process");
const path = require("node:path");
const next = spawn(
  process.execPath,
  [
    require.resolve("next/dist/bin/next"),
    "dev",
    "--hostname",
    "127.0.0.1",
    "--port",
    "3210",
  ],
  { stdio: "inherit" },
);
let desktop;
async function start() {
  for (let i = 0; i < 120; i++) {
    if (next.exitCode !== null) throw new Error("Next dev exited");
    try {
      const r = await fetch("http://127.0.0.1:3210");
      if (r.ok) break;
    } catch {}
    await new Promise((r) => setTimeout(r, 500));
  }
  desktop = spawn(require("electron"), [path.resolve(__dirname, "..")], {
    stdio: "inherit",
    env: { ...process.env, NEXTAGENT_DEV_URL: "http://127.0.0.1:3210" },
  });
  desktop.on("exit", () => next.kill());
}
start().catch((e) => {
  console.error(e);
  next.kill();
  process.exitCode = 1;
});
process.on("SIGINT", () => {
  desktop?.kill();
  next.kill();
});
