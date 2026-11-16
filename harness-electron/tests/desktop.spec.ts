import { test, expect, _electron as electron } from "@playwright/test";
import path from "node:path";
import os from "node:os";
import { mkdtempSync } from "node:fs";
test("desktop renders and executes the native agent loop", async () => {
  const userData = mkdtempSync(
    path.join(os.tmpdir(), "nextagent-electron-ui-"),
  );
  const app = await electron.launch({
    ...(process.env.NEXTAGENT_TEST_EXECUTABLE
      ? { executablePath: process.env.NEXTAGENT_TEST_EXECUTABLE }
      : {}),
    args: [
      ...(process.env.NEXTAGENT_TEST_EXECUTABLE ? [] : [path.resolve(".")]),
      `--user-data-dir=${userData}`,
    ],
    env: { ...process.env, NEXTAGENT_DEV_URL: "", OPENROUTER_API_KEY: "" },
  });
  try {
    const page = await app.firstWindow();
    const errors: string[] = [];
    page.on("pageerror", (e) => errors.push(e.message));
    await expect(
      page.getByRole("heading", { name: "Что будем создавать?" }),
    ).toBeVisible();
    await expect(
      page.getByRole("textbox", { name: "Сообщение" }),
    ).toBeEnabled();
    await expect(
      page.getByText("Ядро подключено", { exact: true }),
    ).toHaveCount(0);
    await page.getByRole("button", { name: "Настройки", exact: true }).click();
    await expect(
      page.getByRole("heading", { name: "Настройки исполнителя" }),
    ).toBeVisible();
    await page
      .getByLabel("API-ключ", { exact: true })
      .fill("sk-or-electron-test-only");
    await page.getByRole("button", { name: "Сохранить", exact: true }).click();
    const config = await page.evaluate(async () => window.harness!.bootstrap());
    expect(config.settings.hasKey).toBe(true);
    expect(JSON.stringify(config)).not.toContain("sk-or-electron-test-only");
    await page.screenshot({ path: "test-results/harness-empty.png" });
    await page.getByRole("button", { name: "Проверить ядро" }).click();
    await expect(page.getByText("Завершено", { exact: true })).toBeVisible({
      timeout: 30000,
    });
    await expect(
      page.getByText("harness-check.txt", { exact: true }).first(),
    ).toBeVisible();
    await expect(page.getByText(/Это автономный self-test/)).toBeVisible();
    await page.screenshot({ path: "test-results/harness-session.png" });
    await page
      .getByRole("button", { name: "harness-check.txt", exact: true })
      .first()
      .click();
    await expect(
      page.getByText("Native NextAgent transaction verified", { exact: true }),
    ).toBeVisible();
    await page.getByRole("button", { name: "Закрыть", exact: true }).click();
    await page.getByRole("button", { name: "Переключить тему" }).click();
    await expect(page.locator("html")).not.toHaveClass(/dark/);
    await page.waitForTimeout(250);
    await page.screenshot({ path: "test-results/harness-light.png" });
    expect(errors).toEqual([]);
  } finally {
    await app.close();
  }
});
