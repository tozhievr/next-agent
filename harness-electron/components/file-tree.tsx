"use client";
import { useEffect, useState } from "react";
import { ChevronRight, FileCode2, Folder, FolderOpen } from "lucide-react";
import { cn } from "@/lib/utils";
import type { Entry } from "@/lib/types";

function label(entry: Entry) {
  return entry.name || entry.path.replace(/\\/g, "/").split("/").pop() || entry.path;
}

function Branch({
  dir,
  depth,
  tick,
  onOpenFile,
}: {
  dir: string;
  depth: number;
  tick: number;
  onOpenFile: (path: string) => void;
}) {
  const [entries, setEntries] = useState<Entry[] | null>(null),
    [failed, setFailed] = useState("");
  useEffect(() => {
    let active = true;
    window.harness
      ?.tree(dir)
      .then((result) => {
        if (!active) return;
        setEntries(
          [...result].sort(
            (a, b) =>
              Number(b.directory) - Number(a.directory) ||
              label(a).localeCompare(label(b)),
          ),
        );
        setFailed("");
      })
      .catch((error: Error) => {
        if (active) setFailed(error.message);
      });
    return () => {
      active = false;
    };
  }, [dir, tick]);
  return (
    <div>
      {failed && (
        <p className="px-2 py-1 text-[10px] text-destructive">{failed}</p>
      )}
      {entries?.map((entry) =>
        entry.directory ? (
          <FolderRow
            key={entry.path}
            entry={entry}
            depth={depth}
            tick={tick}
            onOpenFile={onOpenFile}
          />
        ) : (
          <button
            key={entry.path}
            onClick={() => onOpenFile(entry.path)}
            className="flex w-full items-center gap-1.5 rounded py-1 text-left text-xs text-muted-foreground hover:bg-accent hover:text-foreground"
            style={{ paddingLeft: 8 + depth * 12 }}
            title={entry.path}
          >
            <FileCode2 className="size-3.5 shrink-0" />
            <span className="truncate">{label(entry)}</span>
          </button>
        ),
      )}
      {entries && !entries.length && depth === 0 && (
        <p className="px-2 py-2 text-xs text-muted-foreground">Папка пуста.</p>
      )}
    </div>
  );
}

function FolderRow({
  entry,
  depth,
  tick,
  onOpenFile,
}: {
  entry: Entry;
  depth: number;
  tick: number;
  onOpenFile: (path: string) => void;
}) {
  const [open, setOpen] = useState(false);
  return (
    <div>
      <button
        onClick={() => setOpen(!open)}
        className="flex w-full items-center gap-1 rounded py-1 text-left text-xs hover:bg-accent"
        style={{ paddingLeft: 4 + depth * 12 }}
        title={entry.path}
      >
        <ChevronRight
          className={cn("size-3 shrink-0 text-muted-foreground", open && "rotate-90")}
        />
        {open ? (
          <FolderOpen className="size-3.5 shrink-0 text-muted-foreground" />
        ) : (
          <Folder className="size-3.5 shrink-0 text-muted-foreground" />
        )}
        <span className="truncate">{label(entry)}</span>
      </button>
      {open && (
        <Branch
          dir={entry.path}
          depth={depth + 1}
          tick={tick}
          onOpenFile={onOpenFile}
        />
      )}
    </div>
  );
}

export function FileTree({
  tick,
  onOpenFile,
}: {
  tick: number;
  onOpenFile: (path: string) => void;
}) {
  return <Branch dir="" depth={0} tick={tick} onOpenFile={onOpenFile} />;
}
