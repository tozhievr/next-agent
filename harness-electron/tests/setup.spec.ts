import { test, expect, _electron as electron } from "@playwright/test";
import path from "node:path";
import os from "node:os";
import { mkdtempSync, existsSync } from "node:fs";
test("custom setup installs the verified application into an isolated destination", async () => {
  test.setTimeout(120000);
  test.setTimeout(120000);
  const executable = process.env.NEXTAGENT_SETUP_EXECUTABLE;
  if (!executable) {
    test.skip();
    return;
  }
  const base = mkdtempSync(path.join(os.tmpdir(), "nextagent-setup-ui-"));
  const target = path.join(base, "installed");
  const app = await electron.launch({
    executablePath: executable,
    args: [`--user-data-dir=${path.join(base, "profile")}`],
    env: { ...process.env, NEXTAGENT_SETUP_TEST_TARGET: target },
  });
  app
    .process()
    .stderr?.on("data", (data) => console.log("SETUP STDERR", data.toString()));
  try {
    const page = await app.firstWindow();

    await expect(
      page.getByRole("button", { name: "Установить", exact: true }),
    ).toBeVisible();
    await page.screenshot({ path: "test-results/setup-welcome.png" });
    await page.getByRole("button", { name: "Установить", exact: true }).click();
    await expect(
      page.getByRole("button", { name: "Открыть Harness", exact: true }),
    ).toBeVisible({ timeout: 90000 });
    expect(existsSync(path.join(target, ".nextagent-install.json"))).toBe(true);
    expect(
      existsSync(
        path.join(
          target,
          process.platform === "win32"
            ? "NextAgent Harness.exe"
            : "nextagent-harness",
        ),
      ),
    ).toBe(true);
    await page.screenshot({ path: "test-results/setup-complete.png" });
  } finally {
    await app.close();
  }
});
