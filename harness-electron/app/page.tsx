"use client";
import { useEffect, useRef, useState } from "react";
import {
  Activity,
  ArrowUp,
  ArrowUpRight,
  CheckCheck,
  ChevronDown,
  ChevronRight,
  Code2,
  Command,
  FileCode2,
  Folder,
  FolderOpen,
  GitBranch,
  History,
  Layers,
  Loader2,
  MessageSquare,
  Minus,
  Moon,
  Plus,
  Search,
  Settings2,
  ShieldCheck,
  Square,
  Sun,
  Terminal,
  Workflow,
  X,
  Zap,
} from "lucide-react";
import Markdown from "react-markdown";
import remarkGfm from "remark-gfm";
import { Brand } from "@/components/brand";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Textarea } from "@/components/ui/textarea";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogTitle,
} from "@/components/ui/dialog";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { cn } from "@/lib/utils";
import type {
  Capabilities,
  Entry,
  Opened,
  Session,
  SessionSummary,
  Settings,
} from "@/lib/types";
const empty: Session = {
  messages: [],
  events: [],
  changed: [],
  running: false,
};
const suggestions = [
  {
    icon: Search,
    title: "Разобраться в проекте",
    text: "Изучи архитектуру проекта и объясни, как связаны основные компоненты.",
  },
  {
    icon: Code2,
    title: "Внести изменение",
    text: "Помоги внести изменение в проект. Сначала изучи релевантный код и предложи план.",
  },
  {
    icon: ShieldCheck,
    title: "Найти проблему",
    text: "Проверь проект на ошибки. Найди конкретную проблему, объясни её причину и предложи исправление.",
  },
];
function shortPath(p: string) {
  return p.replace(/\\/g, "/").split("/").filter(Boolean).at(-1) || "Workspace";
}
function Metric({ label, value }: { label: string; value: React.ReactNode }) {
  return (
    <div className="flex items-center justify-between gap-3 py-1.5 text-xs">
      <span className="text-muted-foreground">{label}</span>
      <span className="font-mono tabular-nums">{value}</span>
    </div>
  );
}
function Card({
  title,
  children,
  icon: Icon,
}: {
  title: string;
  children: React.ReactNode;
  icon?: typeof Activity;
}) {
  return (
    <section className="rounded-lg border bg-card/40 p-4">
      <h3 className="mb-3 flex items-center gap-2 text-xs font-medium">
        {Icon && <Icon className="size-3.5 text-muted-foreground" />}
        {title}
      </h3>
      {children}
    </section>
  );
}
export default function Home() {
  const [session, setSession] = useState<Session>(empty),
    [sessions, setSessions] = useState<SessionSummary[]>([]),
    [root, setRoot] = useState("");
  const [settings, setSettings] = useState<Settings>({
      model: "openrouter/auto",
      hasKey: false,
      canStoreKey: false,
    }),
    [caps, setCaps] = useState<Capabilities | null>(null);
  const [prompt, setPrompt] = useState(""),
    [readOnly, setReadOnly] = useState(false),
    [settingsOpen, setSettingsOpen] = useState(false),
    [model, setModel] = useState("openrouter/auto"),
    [apiKey, setApiKey] = useState("");
  const [error, setError] = useState(""),
    [busy, setBusy] = useState(false),
    [entries, setEntries] = useState<Entry[]>([]),
    [treePath, setTreePath] = useState(""),
    [filter, setFilter] = useState(""),
    [file, setFile] = useState<{ path: string; text: string } | null>(null),
    [history, setHistory] = useState<unknown>(null),
    [dark, setDark] = useState(true),
    [tab, setTab] = useState("process"),
    [connected, setConnected] = useState(false);
  const end = useRef<HTMLDivElement>(null),
    textarea = useRef<HTMLTextAreaElement>(null),
    generation = useRef(0);
  const running = session.running;
  async function attempt<T>(fn: () => Promise<T>): Promise<T | undefined> {
    try {
      setError("");
      return await fn();
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    }
  }
  async function loadTree(path = "") {
    const result = await window.harness?.tree(path);
    if (result) {
      setEntries(result);
      setTreePath(path);
    }
  }
  function opened(result: Opened) {
    generation.current++;
    setRoot(result.root);
    setSession(result.session);
    setCaps(result.capabilities);
    setFile(null);
    setEntries([]);
    void loadTree();
  }
  useEffect(() => {
    if (!window.harness) return;
    let active = true;
    window.harness
      .bootstrap()
      .then((data) => {
        if (!active) return;
        setConnected(true);
        setRoot(data.root);
        setSession(data.session);
        setSessions(data.sessions);
        setSettings(data.settings);
        setModel(data.settings.model);
        setCaps(data.capabilities);
        if (data.root) void loadTree();
      })
      .catch((e) => setError(e.message));
    return () => {
      active = false;
    };
  }, []);
  useEffect(() => {
    if (!connected) return;
    let inFlight = false;
    const timer = setInterval(async () => {
      if (inFlight) return;
      inFlight = true;
      const current = generation.current;
      try {
        const s = await window.harness!.snapshot();
        if (current === generation.current) setSession(s);
      } catch (e) {
        setError(String(e));
      } finally {
        inFlight = false;
      }
    }, 400);
    return () => clearInterval(timer);
  }, [connected]);
  useEffect(() => {
    if (!running && connected) {
      void window
        .harness!.sessions()
        .then(setSessions)
        .catch(() => {});
      if (root)
        void window
          .harness!.tree(treePath)
          .then(setEntries)
          .catch(() => {});
    }
  }, [running, connected, root, treePath, session.tool_calls, session.id]);
  useEffect(() => {
    end.current?.scrollIntoView({ behavior: "smooth" });
  }, [session.answer, session.messages.length]);
  async function choose() {
    setBusy(true);
    await attempt(async () => {
      const result = await window.harness!.chooseWorkspace();
      if (result) opened(result);
    });
    setBusy(false);
  }
  async function send() {
    if (!prompt.trim() || running || busy) return;
    if (!root) {
      await choose();
      return;
    }
    if (!settings.hasKey) {
      setSettingsOpen(true);
      return;
    }
    const text = prompt.trim();
    setBusy(true);
    const result = await attempt(() => window.harness!.send(text, readOnly));
    if (result) {
      setSession(result);
      setPrompt("");
    }
    setBusy(false);
  }
  async function read(path: string) {
    await attempt(async () =>
      setFile({ path, ...(await window.harness!.read(path)) }),
    );
  }
  const visibleSessions = sessions.filter(
    (s) =>
      s.workspace === root &&
      s.title.toLowerCase().includes(filter.toLowerCase()),
  );
  const messages = session.messages.filter(
    (m) => m.role === "user" || (m.role === "assistant" && m.content),
  );
  const liveAnswer =
    running && session.answer && messages.at(-1)?.content !== session.answer;
  return (
    <div className="flex h-dvh flex-col bg-background text-foreground">
      <header className="titlebar flex h-12 shrink-0 items-center justify-between border-b px-4">
        <div className="flex items-center gap-2.5 text-xs">
          <div className="flex size-6 items-center justify-center rounded-md border bg-primary text-primary-foreground">
            <Brand className="size-4" />
          </div>
          <span className="font-semibold">NextAgent</span>
          <span className="text-muted-foreground">/</span>
          <span className="text-muted-foreground">Harness</span>
          <span className="ml-2 rounded border px-1.5 py-0.5 font-mono text-[10px] text-muted-foreground">
            DESKTOP
          </span>
        </div>
        <div className="flex items-center gap-4">
          <div className="flex gap-1">
            <Button
              variant="ghost"
              size="icon-sm"
              aria-label="Свернуть"
              onClick={() => window.harness?.window("minimize")}
            >
              <Minus />
            </Button>
            <Button
              variant="ghost"
              size="icon-sm"
              aria-label="Развернуть"
              onClick={() => window.harness?.window("maximize")}
            >
              <Square className="!size-3" />
            </Button>
            <Button
              variant="ghost"
              size="icon-sm"
              aria-label="Закрыть окно"
              onClick={() => window.harness?.window("close")}
            >
              <X />
            </Button>
          </div>
        </div>
      </header>
      <div className="flex min-h-0 flex-1">
        <aside className="flex w-60 shrink-0 flex-col border-r bg-card/20">
          <div className="p-3">
            <Button
              variant="outline"
              className="w-full justify-between"
              disabled={!root || running || busy}
              onClick={() =>
                attempt(async () => {
                  generation.current++;
                  setSession(await window.harness!.newSession());
                })
              }
            >
              <span className="flex items-center gap-2">
                <Plus />
                Новая сессия
              </span>
              <span className="font-mono text-[10px] text-muted-foreground">
                N
              </span>
            </Button>
          </div>
          <div className="px-4 pb-2 pt-3 text-[10px] font-medium tracking-widest text-muted-foreground">
            СЕССИИ
          </div>
          <div className="relative mx-3 mb-2">
            <Search className="absolute left-2.5 top-2 size-3.5 text-muted-foreground" />
            <Input
              aria-label="Поиск сессий"
              value={filter}
              onChange={(e) => setFilter(e.target.value)}
              className="h-8 border-transparent bg-muted/40 pl-8 text-xs shadow-none"
              placeholder="Найти сессию…"
            />
          </div>
          <div className="min-h-28 flex-1 overflow-y-auto px-2">
            {visibleSessions.length ? (
              visibleSessions.map((s) => (
                <button
                  key={s.id}
                  disabled={running || busy}
                  onClick={() =>
                    attempt(async () => {
                      generation.current++;
                      setSession(await window.harness!.restoreSession(s.id));
                    })
                  }
                  className={cn(
                    "mb-1 flex w-full items-center gap-2 rounded-md px-3 py-2.5 text-left text-xs transition-colors hover:bg-accent disabled:opacity-50",
                    s.id === session.id && "bg-accent",
                  )}
                >
                  <MessageSquare className="size-3.5 shrink-0 text-muted-foreground" />
                  <span className="truncate">{s.title || "Новая сессия"}</span>
                </button>
              ))
            ) : (
              <p className="px-3 py-3 text-xs leading-relaxed text-muted-foreground">
                История появится здесь после первого сообщения.
              </p>
            )}
          </div>
          <div className="border-t p-3">
            <div className="mb-3 flex items-center justify-between px-1">
              <span className="text-[10px] font-medium tracking-widest text-muted-foreground">
                WORKSPACE
              </span>
              <Folder className="size-3.5 text-muted-foreground" />
            </div>
            <Button
              variant="outline"
              className="w-full justify-start"
              disabled={!connected || running || busy}
              onClick={choose}
            >
              <FolderOpen />
              {root ? "Сменить папку" : "Открыть папку"}
            </Button>
            {root && (
              <>
                <p
                  className="mt-3 truncate px-1 font-mono text-[10px] text-muted-foreground"
                  title={root}
                >
                  {root}
                </p>
                <div className="mt-2 max-h-48 overflow-y-auto">
                  {treePath && (
                    <button
                      onClick={() =>
                        attempt(() =>
                          loadTree(treePath.replace(/\/?[^/]+\/?$/, "")),
                        )
                      }
                      className="w-full px-2 py-1.5 text-left text-xs text-muted-foreground"
                    >
                      ../
                    </button>
                  )}
                  {entries.map((e) => (
                    <button
                      key={e.path}
                      onClick={() =>
                        e.directory
                          ? attempt(() => loadTree(e.path))
                          : read(e.path)
                      }
                      className="flex w-full items-center gap-2 rounded px-2 py-1.5 text-left text-xs text-muted-foreground hover:bg-accent hover:text-foreground"
                    >
                      {e.directory ? (
                        <Folder className="size-3.5 shrink-0" />
                      ) : (
                        <FileCode2 className="size-3.5 shrink-0" />
                      )}
                      <span className="truncate">
                        {e.name || shortPath(e.path)}
                      </span>
                      {e.directory && (
                        <ChevronRight className="ml-auto size-3" />
                      )}
                    </button>
                  ))}
                </div>
              </>
            )}
          </div>
          <div className="flex items-center gap-2 border-t px-3 py-2.5">
            <div className="flex size-7 items-center justify-center rounded-full bg-secondary">
              <Terminal className="size-3.5" />
            </div>
            <div className="flex-1">
              <p className="text-xs font-medium">Локальное ядро</p>
              <p className="font-mono text-[10px] text-muted-foreground">
                {caps?.version || "next-agent"}
              </p>
            </div>
            <Button
              variant="ghost"
              size="icon-sm"
              aria-label="Настройки"
              onClick={() => setSettingsOpen(true)}
            >
              <Settings2 />
            </Button>
          </div>
        </aside>
        <main className="flex min-w-0 flex-1 flex-col">
          <div className="flex h-14 shrink-0 items-center justify-between gap-3 border-b px-6">
            <div className="flex min-w-0 items-center gap-2 text-xs">
              <Folder className="size-3.5 shrink-0 text-muted-foreground" />
              <span className="truncate text-muted-foreground">
                {root ? shortPath(root) : "Рабочее пространство"}
              </span>
              <ChevronRight className="size-3 text-muted-foreground" />
              <span className="truncate">
                {session.title || "Новая сессия"}
              </span>
            </div>
            <Button
              variant="ghost"
              size="sm"
              disabled={!root}
              onClick={() =>
                attempt(async () => setHistory(await window.harness!.history()))
              }
            >
              <History />
              История
            </Button>
          </div>
          <div className="min-h-0 flex-1 overflow-y-auto px-6 py-8 lg:px-10">
            {!messages.length && !running ? (
              <div className="mx-auto flex min-h-full max-w-2xl flex-col justify-center pb-8">
                <div className="mb-7 flex size-12 items-center justify-center rounded-xl border bg-card shadow-sm">
                  <Brand className="size-7" />
                </div>
                <div className="mb-3 flex items-center gap-2 text-xs text-muted-foreground">
                  <span className="h-px w-5 bg-border" />
                  ВАШЕ РАБОЧЕЕ ПРОСТРАНСТВО
                </div>
                <h1 className="text-3xl font-semibold tracking-tight lg:text-4xl">
                  Что будем создавать?
                </h1>
                <p className="mt-4 max-w-lg text-sm leading-7 text-muted-foreground">
                  Код, контекст и инструменты — в одном месте. Откройте проект и
                  опишите задачу. NextAgent разберётся в деталях.
                </p>
                <div className="mt-8 grid gap-2">
                  {suggestions.map(({ icon: Icon, title, text }) => (
                    <button
                      key={title}
                      onClick={() => {
                        setPrompt(text);
                        textarea.current?.focus();
                      }}
                      className="group flex items-center gap-4 rounded-lg border p-4 text-left transition-colors hover:bg-accent/50"
                    >
                      <div className="flex size-9 shrink-0 items-center justify-center rounded-md bg-muted/60">
                        <Icon className="size-4 text-muted-foreground" />
                      </div>
                      <div className="min-w-0">
                        <p className="text-sm font-medium">{title}</p>
                        <p className="mt-1 truncate text-xs text-muted-foreground">
                          {text}
                        </p>
                      </div>
                      <ArrowUpRight className="ml-auto size-4 shrink-0 text-muted-foreground opacity-0 transition-opacity group-hover:opacity-100" />
                    </button>
                  ))}
                </div>
              </div>
            ) : (
              <div className="mx-auto max-w-3xl space-y-8">
                {messages.map((m, i) => (
                  <article key={i} className="flex gap-3">
                    <div className="flex size-7 shrink-0 items-center justify-center rounded-md border bg-card text-[10px] font-medium">
                      {m.role === "user" ? "ВЫ" : <Brand className="size-4" />}
                    </div>
                    <div className="min-w-0 flex-1">
                      <h2 className="mb-2 text-xs font-semibold">
                        {m.role === "user" ? "Вы" : "NextAgent"}
                      </h2>
                      <div className="markdown">
                        <Markdown
                          remarkPlugins={[remarkGfm]}
                          components={{
                            a: ({ children }) => (
                              <span className="underline">{children}</span>
                            ),
                          }}
                        >
                          {m.content || ""}
                        </Markdown>
                      </div>
                    </div>
                  </article>
                ))}
                {liveAnswer && (
                  <article className="flex gap-3">
                    <Command className="mt-1 size-5 shrink-0" />
                    <div className="markdown min-w-0">
                      <Markdown remarkPlugins={[remarkGfm]}>
                        {session.answer}
                      </Markdown>
                    </div>
                  </article>
                )}
                {running && (
                  <div className="flex items-center gap-2 text-xs text-muted-foreground">
                    <Loader2 className="size-3.5 animate-spin" />
                    {session.status || "Работает…"}
                  </div>
                )}
                <div ref={end} />
              </div>
            )}
          </div>
          <div className="shrink-0 px-6 pb-4 lg:px-10">
            {error && (
              <div
                role="alert"
                className="mx-auto mb-3 flex max-w-3xl items-start justify-between gap-3 rounded-lg border border-destructive/40 bg-destructive/10 p-3 text-xs text-destructive"
              >
                <span>{error}</span>
                <button onClick={() => setError("")} aria-label="Скрыть ошибку">
                  <X className="size-3.5" />
                </button>
              </div>
            )}
            {!connected && (
              <p className="mb-2 text-center text-xs text-muted-foreground">
                Для доступа к ядру запустите Electron: bun run desktop
              </p>
            )}
            <div className="mx-auto max-w-3xl rounded-xl border bg-card/40 p-2.5 shadow-sm focus-within:border-ring/50">
              <Textarea
                ref={textarea}
                aria-label="Сообщение"
                placeholder={
                  root
                    ? "Опишите задачу или задайте вопрос о проекте…"
                    : "Откройте папку проекта, чтобы начать…"
                }
                value={prompt}
                onChange={(e) => {
                  setPrompt(e.target.value);
                  e.target.style.height = "auto";
                  e.target.style.height =
                    Math.min(e.target.scrollHeight, 160) + "px";
                }}
                onKeyDown={(e) => {
                  if (
                    e.key === "Enter" &&
                    !e.shiftKey &&
                    !e.nativeEvent.isComposing
                  ) {
                    e.preventDefault();
                    void send();
                  }
                }}
                disabled={!connected || running}
                rows={1}
                className="min-h-10 max-h-40 border-0 p-1 shadow-none focus-visible:ring-0"
              />
              <div className="flex items-center justify-between gap-2 pt-2">
                <div className="flex min-w-0 items-center gap-1">
                  <Button
                    variant="ghost"
                    size="sm"
                    className="max-w-56 justify-start text-muted-foreground"
                    onClick={() => setSettingsOpen(true)}
                  >
                    <Zap className="!size-3" />
                    <span className="truncate">{settings.model}</span>
                    <ChevronDown className="!size-3" />
                  </Button>
                  <button
                    aria-pressed={readOnly}
                    onClick={() => setReadOnly(!readOnly)}
                    disabled={running}
                    className={cn(
                      "flex items-center gap-1.5 rounded-md px-2 py-1.5 text-[10px] text-muted-foreground hover:bg-accent",
                      readOnly && "bg-accent text-foreground",
                    )}
                  >
                    <ShieldCheck className="size-3" />
                    {readOnly ? "Только чтение" : "Агент"}
                  </button>
                </div>
                {running ? (
                  <Button
                    size="icon-sm"
                    variant="outline"
                    aria-label="Остановить"
                    onClick={() => attempt(() => window.harness!.cancel())}
                  >
                    <Square className="!size-3 fill-current" />
                  </Button>
                ) : (
                  <Button
                    size="icon-sm"
                    aria-label="Отправить"
                    disabled={!connected || !prompt.trim() || busy}
                    onClick={send}
                  >
                    <ArrowUp />
                  </Button>
                )}
              </div>
            </div>
            <p className="mt-2.5 text-center text-[10px] text-muted-foreground">
              Enter — отправить <span className="mx-2 text-border">/</span>{" "}
              Shift + Enter — новая строка{" "}
              <span className="mx-2 text-border">/</span> Изменения сохраняются
              в истории
            </p>
          </div>
        </main>
        <aside className="hidden w-[300px] shrink-0 flex-col border-l bg-card/20 xl:flex">
          <Tabs
            value={tab}
            onValueChange={setTab}
            className="flex min-h-0 flex-1 flex-col p-4"
          >
            <TabsList className="w-full shrink-0">
              <TabsTrigger value="process">Процессы</TabsTrigger>
              <TabsTrigger value="context">Контекст</TabsTrigger>
              <TabsTrigger value="logs">Логи</TabsTrigger>
            </TabsList>
            <TabsContent
              value="process"
              className="min-h-0 space-y-3 overflow-y-auto"
            >
              <Card title="Состояние агента" icon={Activity}>
                <div className="mb-3 flex items-center gap-2 text-sm">
                  {running ? (
                    <Loader2 className="size-3.5 animate-spin" />
                  ) : (
                    <span className="size-1.5 rounded-full bg-foreground" />
                  )}
                  {running
                    ? "Выполняется"
                    : session.error
                      ? "Остановлено с ошибкой"
                      : messages.length
                        ? "Завершено"
                        : "Готов к работе"}
                </div>
                <p className="text-xs leading-5 text-muted-foreground">
                  {running
                    ? session.status
                    : messages.length
                      ? "Результат сохранён в истории сессии."
                      : "Здесь появятся шаги выполнения и результаты инструментов."}
                </p>
                {session.events.slice(-4).map((e, i) => (
                  <div key={i} className="mt-3 border-t pt-3">
                    <div className="flex items-center justify-between gap-2 text-xs">
                      <span className="truncate">{e.title}</span>
                      <span className="font-mono text-[10px] text-muted-foreground">
                        {e.seconds.toFixed(2)}s
                      </span>
                    </div>
                  </div>
                ))}
              </Card>
              <Card title="Исполнитель" icon={Workflow}>
                <div className="flex items-center justify-between text-xs">
                  <span>OpenRouter</span>
                  <span className="rounded border px-1.5 py-0.5 text-[9px] text-muted-foreground">
                    PROVIDER
                  </span>
                </div>
                <p
                  className="mt-2 truncate font-mono text-[11px] text-muted-foreground"
                  title={settings.model}
                >
                  {settings.model}
                </p>
                <div className="mt-4 flex items-center gap-1.5 text-[10px] text-muted-foreground">
                  <ShieldCheck className="size-3" />
                  {session.last_check
                    ? `Проверки: рев. ${session.last_check.checked_revision}${session.last_check.fresh ? "" : " · требуют обновления"}`
                    : "Проверки ещё не запускались"}
                </div>
              </Card>
              <Card title="Метрики сессии" icon={Zap}>
                <Metric
                  label="Время"
                  value={`${(session.elapsed || 0).toFixed(1)} с`}
                />
                <Metric
                  label="Входные токены"
                  value={(session.input_tokens || 0).toLocaleString()}
                />
                <Metric
                  label="Из кеша провайдера"
                  value={(session.cached_tokens || 0).toLocaleString()}
                />
                <Metric
                  label="Записано в кеш"
                  value={(session.cache_write_tokens || 0).toLocaleString()}
                />
                <Metric
                  label="Выходные токены"
                  value={(session.output_tokens || 0).toLocaleString()}
                />
                <Metric
                  label="Вызовы инструментов"
                  value={session.tool_calls || 0}
                />
                <div className="mt-2 border-t pt-2">
                  <Metric
                    label="Стоимость"
                    value={
                      session.cost_reported
                        ? `$${(session.cost || 0).toFixed(4)}`
                        : "—"
                    }
                  />
                </div>
              </Card>
              <Card
                title={`Изменённые файлы · ${session.changed.length}`}
                icon={GitBranch}
              >
                {session.changed.length ? (
                  session.changed.map((path) => (
                    <button
                      key={path}
                      onClick={() => read(path)}
                      className="flex w-full items-center gap-2 py-2 text-left font-mono text-[10px] hover:underline"
                    >
                      <FileCode2 className="size-3 shrink-0" />
                      <span className="truncate">{path}</span>
                    </button>
                  ))
                ) : (
                  <p className="text-xs leading-5 text-muted-foreground">
                    Изменений пока нет.
                  </p>
                )}
              </Card>
            </TabsContent>
            <TabsContent value="context" className="min-h-0 overflow-y-auto">
              <Card title="Языковое ядро" icon={Layers}>
                <p className="mb-3 text-xs leading-5 text-muted-foreground">
                  Модули загружаются по расширению файла внутри процесса.
                </p>
                <div className="flex flex-wrap gap-1.5">
                  {caps?.modules.map((m) => (
                    <span
                      key={m.name}
                      className="rounded border px-2 py-1 font-mono text-[10px] text-muted-foreground"
                    >
                      {m.name}
                      {m.loaded ? " •" : ""}
                    </span>
                  )) || (
                    <span className="text-xs text-muted-foreground">
                      Откройте проект для загрузки контекста.
                    </span>
                  )}
                </div>
              </Card>
              <Button
                variant="outline"
                size="sm"
                className="mt-3 w-full"
                disabled={!root}
                onClick={() =>
                  attempt(async () =>
                    setHistory(await window.harness!.labels()),
                  )
                }
              >
                Показать AST labels
              </Button>
            </TabsContent>
            <TabsContent value="logs" className="min-h-0 overflow-y-auto">
              {session.events.length ? (
                session.events.map((e, i) => (
                  <details key={i} className="mb-2 rounded-lg border p-3">
                    <summary className="cursor-pointer text-xs">
                      {e.title}
                    </summary>
                    <pre className="mt-2 whitespace-pre-wrap break-all font-mono text-[10px] text-muted-foreground">
                      {e.detail}
                    </pre>
                  </details>
                ))
              ) : (
                <p className="p-2 text-xs text-muted-foreground">
                  События появятся после запуска задачи.
                </p>
              )}
            </TabsContent>
          </Tabs>
        </aside>
      </div>
      <footer className="flex h-7 shrink-0 items-center justify-between border-t px-4 text-[10px] text-muted-foreground">
        <span className="flex items-center gap-2">
          <Terminal className="size-3" />
          next-agent + next-langserver{" "}
          <span className="mx-1 text-border">|</span>{" "}
          {root ? shortPath(root) : "Нет открытого проекта"}
        </span>
        <div className="flex items-center gap-3">
          <button
            disabled={!connected || running || busy}
            onClick={() =>
              attempt(async () => opened(await window.harness!.selfTest()))
            }
            className="flex items-center gap-1.5 hover:text-foreground"
          >
            <CheckCheck className="size-3" />
            Проверить ядро
          </button>
          <span className="text-border">|</span>
          <button
            aria-label="Переключить тему"
            onClick={() => {
              setDark(!dark);
              document.documentElement.classList.toggle("dark", !dark);
            }}
          >
            {dark ? <Moon className="size-3" /> : <Sun className="size-3" />}
          </button>
        </div>
      </footer>
      <Dialog open={settingsOpen} onOpenChange={setSettingsOpen}>
        <DialogContent>
          <DialogTitle className="text-lg font-semibold">
            Настройки исполнителя
          </DialogTitle>
          <DialogDescription className="text-sm text-muted-foreground">
            Подключите модель с поддержкой инструментов через OpenRouter.
          </DialogDescription>
          <div className="space-y-4 py-2">
            <label className="grid gap-2 text-xs font-medium">
              Модель
              <Input
                value={model}
                onChange={(e) => setModel(e.target.value)}
                placeholder="openrouter/auto"
              />
            </label>
            <label className="grid gap-2 text-xs font-medium">
              API-ключ
              <Input
                type="password"
                autoComplete="off"
                value={apiKey}
                onChange={(e) => setApiKey(e.target.value)}
                placeholder={
                  settings.hasKey
                    ? "Ключ настроен. Введите новый для замены"
                    : "sk-or-…"
                }
              />
            </label>
            <p className="text-xs leading-5 text-muted-foreground">
              {settings.canStoreKey
                ? "Ключ шифруется системным хранилищем и остаётся в основном процессе Electron."
                : "Шифрование недоступно: новый ключ будет храниться только до закрытия приложения."}
            </p>
          </div>
          <div className="flex justify-between gap-2">
            <Button
              variant="ghost"
              disabled={!settings.hasKey}
              onClick={() =>
                attempt(async () => {
                  setSettings(
                    await window.harness!.settings({ model, clearKey: true }),
                  );
                  setApiKey("");
                })
              }
            >
              Удалить ключ
            </Button>
            <Button
              disabled={!connected}
              onClick={() =>
                attempt(async () => {
                  setSettings(
                    await window.harness!.settings({ model, apiKey }),
                  );
                  setApiKey("");
                  setSettingsOpen(false);
                })
              }
            >
              Сохранить
            </Button>
          </div>
        </DialogContent>
      </Dialog>
      <Dialog
        open={!!file}
        onOpenChange={(open) => {
          if (!open) setFile(null);
        }}
      >
        <DialogContent className="max-w-4xl">
          <DialogTitle className="flex items-center gap-2 text-sm">
            <FileCode2 className="size-4" />
            {file?.path}
          </DialogTitle>
          <DialogDescription className="text-xs text-muted-foreground">
            Содержимое получено через next-agent.
          </DialogDescription>
          <pre className="max-h-[65vh] overflow-auto rounded-md border bg-muted/40 p-4 font-mono text-xs leading-6">
            {file?.text}
          </pre>
        </DialogContent>
      </Dialog>
      <Dialog
        open={history !== null}
        onOpenChange={(open) => {
          if (!open) setHistory(null);
        }}
      >
        <DialogContent className="max-w-3xl">
          <DialogTitle className="text-sm">История и метаданные</DialogTitle>
          <DialogDescription className="text-xs text-muted-foreground">
            Данные журнала next-agent.
          </DialogDescription>
          <pre className="max-h-[65vh] overflow-auto rounded-md border bg-muted/40 p-4 font-mono text-xs">
            {JSON.stringify(history, null, 2)}
          </pre>
        </DialogContent>
      </Dialog>
    </div>
  );
}
