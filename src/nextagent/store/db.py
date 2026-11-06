"""Durable SQLite revision store for the initial M0 editor."""

import json
import os
import sqlite3
import tempfile
import time
from hashlib import blake2b
from pathlib import Path
from collections.abc import Callable

from nextagent.core.changeset import ChangeSet, Edit
from nextagent.core.snapshot import Snapshot

SCHEMA = """
CREATE TABLE IF NOT EXISTS workspace (
 id INTEGER PRIMARY KEY, root TEXT NOT NULL UNIQUE
);
CREATE TABLE IF NOT EXISTS branch (
 id INTEGER PRIMARY KEY, ws_id INTEGER NOT NULL REFERENCES workspace(id),
 name TEXT NOT NULL, head_rev INTEGER, UNIQUE(ws_id,name)
);
CREATE TABLE IF NOT EXISTS doc (
 id INTEGER PRIMARY KEY, ws_id INTEGER NOT NULL REFERENCES workspace(id),
 path TEXT NOT NULL, UNIQUE(ws_id,path)
);
CREATE TABLE IF NOT EXISTS txn (
 id INTEGER PRIMARY KEY, session_id TEXT NOT NULL, label TEXT,
 status TEXT NOT NULL CHECK(status IN ('open','committed','rolled_back')),
 ts INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS rev (
 id INTEGER PRIMARY KEY, branch_id INTEGER NOT NULL REFERENCES branch(id),
 parent INTEGER REFERENCES rev(id), txn_id INTEGER REFERENCES txn(id), ts INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS doc_head (
 doc_id INTEGER NOT NULL REFERENCES doc(id), branch_id INTEGER NOT NULL REFERENCES branch(id),
 rev_id INTEGER NOT NULL REFERENCES rev(id), text TEXT NOT NULL, hash BLOB NOT NULL,
 PRIMARY KEY(doc_id,branch_id)
);
CREATE TABLE IF NOT EXISTS disk_baseline (
 doc_id INTEGER PRIMARY KEY REFERENCES doc(id), disk_hash BLOB,
 exists_on_disk INTEGER NOT NULL CHECK(exists_on_disk IN (0,1))
);
CREATE TABLE IF NOT EXISTS doc_delta (
 doc_id INTEGER NOT NULL REFERENCES doc(id), rev_id INTEGER NOT NULL REFERENCES rev(id),
 forward TEXT NOT NULL, inverse TEXT NOT NULL, PRIMARY KEY(doc_id,rev_id)
);
"""


def _encode(change: ChangeSet) -> str:
    return json.dumps([[e.start, e.end, e.text] for e in change.edits], ensure_ascii=False)


def _decode(payload: str) -> ChangeSet:
    return ChangeSet(tuple(Edit(*values) for values in json.loads(payload)))


class Store:
    """Synchronous single-process prototype; asyncio writer is planned for M2."""

    def __init__(self, root: Path, db_path: Path | None = None) -> None:
        self.root = root.resolve()
        self.db_path = db_path or (self.root / ".aed" / "state.sqlite3")
        self.db_path.parent.mkdir(parents=True, exist_ok=True)
        self.db = sqlite3.connect(self.db_path)
        self.db.execute("PRAGMA journal_mode=WAL")
        self.db.execute("PRAGMA synchronous=NORMAL")
        self.db.execute("PRAGMA foreign_keys=ON")
        self.db.executescript(SCHEMA)
        self.db.execute("UPDATE txn SET status='rolled_back' WHERE status='open'")
        self.db.execute("INSERT OR IGNORE INTO workspace(root) VALUES (?)", (str(self.root),))
        self.ws_id = self.db.execute(
            "SELECT id FROM workspace WHERE root=?", (str(self.root),)
        ).fetchone()[0]
        self.db.execute(
            "INSERT OR IGNORE INTO branch(ws_id,name) VALUES (?,'main')", (self.ws_id,)
        )
        self.branch_id = self.db.execute(
            "SELECT id FROM branch WHERE ws_id=? AND name='main'", (self.ws_id,)
        ).fetchone()[0]
        self.db.commit()

    def __enter__(self) -> "Store":
        return self

    def __exit__(self, *_exc: object) -> None:
        self.db.close()

    def _path(self, name: str) -> Path:
        candidate = (self.root / name).resolve()
        if candidate == self.root or not candidate.is_relative_to(self.root):
            raise ValueError("path escapes workspace")
        parts = candidate.relative_to(self.root).parts
        if candidate == self.db_path.resolve() or ".git" in parts or ".aed" in parts:
            raise ValueError("reserved workspace path")
        return candidate

    def _doc(self, name: str) -> tuple[int, Snapshot]:
        disk = self._path(name)
        name = disk.relative_to(self.root).as_posix()
        row = self.db.execute(
            "SELECT id FROM doc WHERE ws_id=? AND path=?", (self.ws_id, name)
        ).fetchone()
        if row:
            doc_id = row[0]
        else:
            self.db.execute("INSERT INTO doc(ws_id,path) VALUES (?,?)", (self.ws_id, name))
            doc_id = self.db.execute("SELECT last_insert_rowid()").fetchone()[0]
        baseline = self.db.execute(
            "SELECT 1 FROM disk_baseline WHERE doc_id=?", (doc_id,)
        ).fetchone()
        if baseline is None:
            exists = disk.exists()
            digest = self._disk_hash(disk) if exists else None
            self.db.execute(
                "INSERT INTO disk_baseline(doc_id,disk_hash,exists_on_disk) VALUES (?,?,?)",
                (doc_id, digest, int(exists)),
            )
        head = self.db.execute(
            "SELECT rev_id,text FROM doc_head WHERE doc_id=? AND branch_id=?",
            (doc_id, self.branch_id),
        ).fetchone()
        if head:
            return doc_id, Snapshot.of(doc_id, head[0], head[1])
        text = disk.read_text(encoding="utf-8") if disk.exists() else ""
        return doc_id, Snapshot.of(doc_id, 0, text)

    @staticmethod
    def _disk_hash(path: Path) -> bytes:
        return blake2b(path.read_bytes(), digest_size=16).digest()

    def view(self, name: str) -> Snapshot:
        snap = self._doc(name)[1]
        self.db.commit()
        return snap

    def head(self) -> int:
        row = self.db.execute(
            "SELECT head_rev FROM branch WHERE id=?", (self.branch_id,)
        ).fetchone()
        return row[0] or 0

    def edit(
        self,
        edits: dict[str, ChangeSet],
        *,
        if_rev: int | None = None,
        label: str = "edit",
        session_id: str = "cli",
        validator: Callable[[dict[str, tuple[Snapshot, Snapshot]]], None] | None = None,
    ) -> dict:
        """Commit all file changes as a single SQLite transaction."""
        if not edits:
            raise ValueError("empty edit batch")
        if len({self._path(name) for name in edits}) != len(edits):
            raise ValueError("duplicate canonical paths in edit batch")
        try:
            self.db.execute("BEGIN IMMEDIATE")
            parent = self.head()
            if if_rev is not None and if_rev != parent:
                raise ValueError(f"stale_rev: expected {if_rev}, current {parent}")
            updated = []
            for name, change in edits.items():
                doc_id, snap = self._doc(name)
                updated.append((name, doc_id, change.apply(snap), change, change.invert(snap)))
            if validator is not None:
                validator({name: (self._doc(name)[1], snap) for name, _, snap, _, _ in updated})
            timestamp = int(time.time())
            txn_id = self.db.execute(
                "INSERT INTO txn(session_id,label,status,ts) VALUES (?,?,'open',?)",
                (session_id, label, timestamp),
            ).lastrowid
            rev_id = self.db.execute(
                "INSERT INTO rev(branch_id,parent,txn_id,ts) VALUES (?,?,?,?)",
                (self.branch_id, parent or None, txn_id, timestamp),
            ).lastrowid
            for _, doc_id, snap, forward, inverse in updated:
                self.db.execute(
                    "INSERT INTO doc_head(doc_id,branch_id,rev_id,text,hash) VALUES (?,?,?,?,?) "
                    "ON CONFLICT(doc_id,branch_id) DO UPDATE SET "
                    "rev_id=excluded.rev_id,text=excluded.text,hash=excluded.hash",
                    (doc_id, self.branch_id, rev_id, snap.text, snap.hash),
                )
                self.db.execute(
                    "INSERT INTO doc_delta(doc_id,rev_id,forward,inverse) VALUES (?,?,?,?)",
                    (doc_id, rev_id, _encode(forward), _encode(inverse)),
                )
            self.db.execute("UPDATE branch SET head_rev=? WHERE id=?", (rev_id, self.branch_id))
            self.db.execute("UPDATE txn SET status='committed' WHERE id=?", (txn_id,))
            self.db.commit()
            return {"ok": True, "rev": rev_id, "paths": [item[0] for item in updated]}
        except BaseException:
            self.db.rollback()
            raise

    def undo(self) -> dict:
        """Append the inverse of HEAD to preserve an auditable history."""
        rev_id = self.head()
        if not rev_id:
            raise ValueError("no revisions to undo")
        rows = self.db.execute(
            "SELECT doc.path,d.inverse FROM doc_delta AS d "
            "JOIN doc ON doc.id=d.doc_id WHERE d.rev_id=?", (rev_id,)
        ).fetchall()
        if not rows:
            raise ValueError("revision has no changes")
        return self.edit(
            {name: _decode(inverse) for name, inverse in rows},
            if_rev=rev_id,
            label=f"undo revision {rev_id}",
        )

    def history(self, limit: int = 20) -> list[dict]:
        rows = self.db.execute(
            "SELECT rev.id,rev.parent,txn.label,txn.ts FROM rev "
            "JOIN txn ON txn.id=rev.txn_id WHERE rev.branch_id=? "
            "ORDER BY rev.id DESC LIMIT ?",
            (self.branch_id, limit),
        ).fetchall()
        return [
            dict(zip(("rev", "parent", "label", "ts"), row, strict=True)) for row in rows
        ]

    def flush(self, paths: list[str] | None = None) -> list[str]:
        """Atomically flush files, rejecting externally modified disk baselines."""
        rows = self.db.execute(
            "SELECT doc.id,doc.path,head.text,base.disk_hash,base.exists_on_disk "
            "FROM doc_head AS head JOIN doc ON doc.id=head.doc_id "
            "JOIN disk_baseline AS base ON base.doc_id=doc.id "
            "WHERE head.branch_id=?", (self.branch_id,)
        ).fetchall()
        allowed = set(paths) if paths is not None else None
        selected = [row for row in rows if allowed is None or row[1] in allowed]

        # Detect conflicts across ALL selected paths before writing any of them.
        for _, name, _, expected_hash, expected_exists in selected:
            path = self._path(name)
            exists = path.exists()
            if exists != bool(expected_exists) or (
                exists and self._disk_hash(path) != expected_hash
            ):
                raise ValueError(f"disk_conflict: external modification of {name}")

        written = []
        for doc_id, name, content, expected_hash, expected_exists in selected:
            path = self._path(name)
            path.parent.mkdir(parents=True, exist_ok=True)
            fd, temporary = tempfile.mkstemp(prefix=".aed-", dir=path.parent)
            try:
                with os.fdopen(fd, "w", encoding="utf-8", newline="") as stream:
                    stream.write(content)
                    stream.flush()
                    os.fsync(stream.fileno())
                if expected_exists:
                    os.chmod(temporary, path.stat().st_mode)
                # Recheck immediately before replacement; a watcher/merge is M4.
                if path.exists() != bool(expected_exists) or (
                    expected_exists and self._disk_hash(path) != expected_hash
                ):
                    raise ValueError(f"disk_conflict: external modification of {name}")
                os.replace(temporary, path)
            finally:
                if os.path.exists(temporary):
                    os.unlink(temporary)
            self.db.execute(
                "UPDATE disk_baseline SET disk_hash=?,exists_on_disk=1 WHERE doc_id=?",
                (self._disk_hash(path), doc_id),
            )
            written.append(name)
        self.db.commit()
        return written
