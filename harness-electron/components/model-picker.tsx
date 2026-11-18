"use client";
import { useEffect, useRef, useState } from "react";
import { ChevronDown, Loader2, Zap } from "lucide-react";
import { cn } from "@/lib/utils";
import type { CatalogModel } from "@/lib/types";

export function ModelPicker({
  value,
  models,
  loading,
  error,
  disabled,
  onOpen,
  onSelect,
  compact,
  upward,
}: {
  value: string;
  models: CatalogModel[];
  loading: boolean;
  error: string;
  disabled?: boolean;
  onOpen: () => void;
  onSelect: (id: string) => void;
  compact?: boolean;
  upward?: boolean;
}) {
  const [open, setOpen] = useState(false),
    [query, setQuery] = useState(""),
    [active, setActive] = useState(0);
  const root = useRef<HTMLDivElement>(null),
    search = useRef<HTMLInputElement>(null);
  useEffect(() => {
    if (!open) return;
    const close = (event: MouseEvent) => {
      if (!root.current?.contains(event.target as Node)) setOpen(false);
    };
    window.addEventListener("mousedown", close);
    return () => window.removeEventListener("mousedown", close);
  }, [open]);
  const needle = query.trim().toLowerCase();
  const shown = models
    .filter(
      (model) =>
        !needle ||
        model.id.toLowerCase().includes(needle) ||
        model.name.toLowerCase().includes(needle),
    )
    .slice(0, 80);
  const typed =
    needle.includes("/") && !models.some((model) => model.id === query.trim())
      ? query.trim()
      : "";
  const rows = typed ? shown.length + 1 : shown.length;
  function choose(id: string) {
    onSelect(id);
    setOpen(false);
    setQuery("");
  }
  return (
    <div ref={root} className={cn("relative", compact ? "min-w-0" : "w-full")}>
      <button
        type="button"
        disabled={disabled}
        aria-haspopup="listbox"
        aria-expanded={open}
        onClick={() => {
          const next = !open;
          setOpen(next);
          setActive(0);
          if (next) {
            onOpen();
            setTimeout(() => search.current?.focus(), 0);
          }
        }}
        className={cn(
          "flex items-center gap-1.5 text-left disabled:opacity-50",
          compact
            ? "max-w-56 rounded-md px-2 py-1.5 text-xs text-muted-foreground hover:bg-accent"
            : "h-9 w-full rounded-md border bg-transparent px-3 text-sm",
        )}
      >
        {compact && <Zap className="size-3 shrink-0" />}
        <span className="min-w-0 flex-1 truncate">{value || "Модель"}</span>
        <ChevronDown className="size-3 shrink-0" />
      </button>
      {open && (
        <div
          className={cn(
            "absolute left-0 z-50 w-80 overflow-hidden rounded-lg border bg-popover text-popover-foreground shadow-md",
            upward ? "bottom-full mb-2" : "top-full mt-2",
          )}
        >
          <input
            ref={search}
            aria-label="Поиск модели"
            value={query}
            onChange={(event) => {
              setQuery(event.target.value);
              setActive(0);
            }}
            onKeyDown={(event) => {
              if (event.key === "ArrowDown") {
                event.preventDefault();
                setActive((index) => Math.min(index + 1, Math.max(rows - 1, 0)));
              } else if (event.key === "ArrowUp") {
                event.preventDefault();
                setActive((index) => Math.max(index - 1, 0));
              } else if (event.key === "Escape") setOpen(false);
              else if (event.key === "Enter") {
                event.preventDefault();
                const id =
                  active === shown.length && typed
                    ? typed
                    : shown[active]?.id;
                if (id) choose(id);
              }
            }}
            placeholder="Найти модель…"
            className="h-9 w-full border-b bg-transparent px-3 text-xs outline-none"
          />
          <div role="listbox" className="max-h-72 overflow-y-auto p-1">
            {loading && (
              <p className="flex items-center gap-2 px-2 py-3 text-xs text-muted-foreground">
                <Loader2 className="size-3.5 animate-spin" />
                Загрузка каталога OpenRouter…
              </p>
            )}
            {error && (
              <p className="px-2 py-2 text-xs text-destructive">{error}</p>
            )}
            {shown.map((model, index) => (
              <button
                key={model.id}
                type="button"
                role="option"
                aria-selected={model.id === value}
                onMouseEnter={() => setActive(index)}
                onClick={() => choose(model.id)}
                className={cn(
                  "flex w-full flex-col rounded-md px-2 py-1.5 text-left",
                  index === active && "bg-accent",
                )}
              >
                <span className="truncate text-xs">{model.name}</span>
                <span className="truncate font-mono text-[10px] text-muted-foreground">
                  {model.id}
                  {model.context
                    ? ` · ${Math.round(model.context / 1000)}k`
                    : ""}
                  {model.tools ? "" : " · без tools"}
                </span>
              </button>
            ))}
            {typed && (
              <button
                type="button"
                onMouseEnter={() => setActive(shown.length)}
                onClick={() => choose(typed)}
                className={cn(
                  "w-full rounded-md px-2 py-1.5 text-left text-xs",
                  active === shown.length && "bg-accent",
                )}
              >
                Использовать {typed}
              </button>
            )}
            {!loading && !shown.length && !typed && (
              <p className="px-2 py-3 text-xs text-muted-foreground">
                Ничего не найдено.
              </p>
            )}
          </div>
        </div>
      )}
    </div>
  );
}
