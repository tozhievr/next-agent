"use client";
import { useEffect, useState } from "react";
import { ArrowRight, Check, FolderOpen, X } from "lucide-react";
import { Brand } from "@/components/brand";
import { Button } from "@/components/ui/button";
type Status = {
  mode?: string;
  phase: string;
  bytes: number;
  total: number;
  target: string;
  version: string;
  error: string;
};
declare global {
  interface Window {
    setup?: {
      status(): Promise<Status>;
      choose(): Promise<Status>;
      install(): Promise<Status>;
      close(): Promise<Status>;
      launch(): Promise<Status>;
    };
  }
}
export default function Setup() {
  const [status, setStatus] = useState<Status>({
    phase: "welcome",
    bytes: 0,
    total: 0,
    target: "",
    version: "",
    error: "",
  });
  const [error, setError] = useState("");
  useEffect(() => {
    let inFlight = false;
    const poll = async () => {
      if (inFlight || !window.setup) return;
      inFlight = true;
      try {
        setStatus(await window.setup.status());
      } catch (e) {
        setError(String(e));
      } finally {
        inFlight = false;
      }
    };
    void poll();
    const timer = setInterval(poll, 100);
    return () => clearInterval(timer);
  }, []);
  async function action(fn: () => Promise<Status>) {
    try {
      setError("");
      setStatus(await fn());
    } catch (e) {
      setError(String(e));
    }
  }
  const removing = status.mode === "uninstall";
  const active = status.phase === "copy",
    done = status.phase === "complete",
    progress = status.total
      ? Math.round((status.bytes / status.total) * 100)
      : 0;
  return (
    <div className="flex h-dvh flex-col bg-background">
      <header className="titlebar flex h-12 items-center justify-between border-b px-5">
        <span className="flex items-center gap-2 text-xs">
          <Brand className="size-5" />
          NextAgent Harness{" "}
          <span className="ml-2 text-muted-foreground">Установка</span>
        </span>
        <Button
          variant="ghost"
          size="icon-sm"
          aria-label="Закрыть установщик"
          onClick={() => window.setup && action(window.setup.close)}
        >
          <X />
        </Button>
      </header>
      <div className="grid min-h-0 flex-1 grid-cols-[300px_1fr]">
        <aside className="relative flex flex-col items-center justify-center overflow-hidden border-r bg-card/50">
          <div className="setup-orbit absolute size-64 rounded-full border border-dashed border-foreground/10" />
          <div className="setup-orbit-reverse absolute size-48 rounded-full border border-foreground/10" />
          <div className="setup-grid absolute inset-0 opacity-30" />
          <div className="setup-emblem relative flex size-24 items-center justify-center rounded-3xl border bg-background shadow-2xl">
            <Brand className="size-16" />
          </div>
          <p className="relative mt-8 text-lg font-semibold tracking-tight">
            NextAgent Harness
          </p>
          <p className="relative mt-2 text-xs text-muted-foreground">
            Ваше пространство для кода.
          </p>
          <div className="relative mt-12 flex gap-1.5">
            {[0, 1, 2, 3, 4, 5, 6].map((i) => (
              <span
                key={i}
                className="setup-wave h-1 w-4 rounded-full bg-foreground/40"
                style={{ animationDelay: `${i * 110}ms` }}
              />
            ))}
          </div>
        </aside>
        <main className="flex flex-col justify-center px-10">
          <p className="mb-3 font-mono text-[10px] tracking-[.2em] text-muted-foreground">
            {done
              ? "ГОТОВО К РАБОТЕ"
              : active
                ? "НАСТРАИВАЕМ ПРОСТРАНСТВО"
                : "НАЧНЁМ С ГЛАВНОГО"}
          </p>
          <h1 className="text-3xl font-semibold tracking-tight">
            {removing
              ? done
                ? "Приложение удалено."
                : active
                  ? "Удаляем приложение…"
                  : "Удалить Harness?"
              : done
                ? "Всё готово."
                : active
                  ? "Устраивайтесь удобнее."
                  : "Меньше рутины.\nБольше результата."}
          </h1>
          <p className="mt-4 text-sm leading-6 text-muted-foreground">
            {removing
              ? "Будет удалено только приложение. Ваши проекты и история сессий сохранятся."
              : done
                ? "Приложение установлено. Откройте проект и начните с первой задачи."
                : active
                  ? "Копируем приложение и проверяем целостность каждого файла. Настройки и проекты останутся на месте."
                  : "Агент, код и инструменты — в одном приложении. Выберите папку установки, остальное мы подготовим."}
          </p>
          <div className="mt-7 rounded-lg border p-3">
            <div className="flex items-center justify-between text-xs">
              <span className="text-muted-foreground">Папка установки</span>
              <button
                aria-label="Изменить папку установки"
                disabled={active || done || removing}
                onClick={() => window.setup && action(window.setup.choose)}
                className="hover:text-muted-foreground"
              >
                <FolderOpen className="size-3.5" />
              </button>
            </div>
            <p
              className="mt-2 truncate font-mono text-[10px]"
              title={status.target}
            >
              {status.target || "Подготовка…"}
            </p>
          </div>
          {active && (
            <div className="mt-5">
              <div className="mb-2 flex justify-between text-xs">
                <span className="text-muted-foreground">
                  Установка и проверка
                </span>
                <span className="font-mono">{progress}%</span>
              </div>
              <div className="h-1.5 overflow-hidden rounded-full bg-muted">
                <div
                  className="h-full rounded-full bg-foreground transition-[width] duration-200"
                  style={{ width: `${progress}%` }}
                />
              </div>
              <p className="mt-2 font-mono text-[10px] text-muted-foreground">
                {(status.bytes / 1024 / 1024).toFixed(1)} /{" "}
                {(status.total / 1024 / 1024).toFixed(1)} MB
              </p>
            </div>
          )}
          {(error || status.error) && (
            <p role="alert" className="mt-3 text-xs text-destructive">
              {error || status.error}
            </p>
          )}
          <Button
            className="mt-7 w-full"
            disabled={active || !status.target}
            onClick={() =>
              window.setup &&
              action(done ? window.setup.launch : window.setup.install)
            }
          >
            {done ? (
              <>
                <Check />
                {removing ? "Закрыть" : "Открыть Harness"}
              </>
            ) : (
              <>
                {removing ? "Удалить приложение" : "Установить"}
                <ArrowRight />
              </>
            )}
          </Button>
          <p className="mt-3 text-center text-[10px] text-muted-foreground">
            {active
              ? "Закрытие отменит незавершённую установку."
              : `Версия ${status.version} · Только для текущего пользователя`}
          </p>
        </main>
      </div>
    </div>
  );
}
