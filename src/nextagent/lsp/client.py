"""Minimal clangd subprocess client with request correlation and diagnostics.

This is a synchronous, single-owner client. It does not apply workspace edits.
"""
import queue
import time
import shutil
import subprocess
import threading
from pathlib import Path
from typing import Any

from nextagent.lsp.framing import read_message, write_message


class ClangdClient:
    def __init__(self, root: Path, executable: str = "clangd", timeout: float = 10.0):
        self.root = root.resolve()
        self.executable = executable
        self.timeout = timeout
        self.process: subprocess.Popen[bytes] | None = None
        self._pending: dict[int, queue.Queue[dict]] = {}
        self._diagnostics: dict[str, dict] = {}
        self._diagnostics_changed = threading.Condition(self._lock)
        self._next_id = 0
        self._lock = threading.Lock()
        self._reader: threading.Thread | None = None
        self._reader_error: BaseException | None = None
        self.encoding = "utf-16"

    def __enter__(self) -> "ClangdClient":
        self.start()
        return self

    def __exit__(self, *_exc: object) -> None:
        self.close()

    def start(self) -> None:
        if self.process is not None:
            raise RuntimeError("clangd already started")
        if shutil.which(self.executable) is None:
            raise FileNotFoundError(f"clangd executable not found: {self.executable}")
        self.process = subprocess.Popen(
            [self.executable, "--background-index"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            cwd=self.root,
        )
        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._reader.start()
        try:
            result = self.request("initialize", {
                "processId": None,
                "rootUri": self.root.as_uri(),
                "capabilities": {"general": {
                    "positionEncodings": ["utf-8", "utf-16", "utf-32"]
                }},
            })
            self.encoding = result.get("capabilities", {}).get("positionEncoding", "utf-16")
            if self.encoding not in ("utf-8", "utf-16", "utf-32"):
                raise ValueError(f"unsupported clangd position encoding: {self.encoding}")
            self.notify("initialized", {})
        except BaseException:
            self.close()
            raise

    def _read_loop(self) -> None:
        assert self.process is not None and self.process.stdout is not None
        try:
            while (message := read_message(self.process.stdout)) is not None:
                if not isinstance(message, dict):
                    continue
                if "id" in message:
                    with self._lock:
                        waiter = self._pending.get(message["id"])
                    if waiter is not None:
                        waiter.put(message)
                elif message.get("method") == "textDocument/publishDiagnostics":
                    params = message.get("params", {})
                    if isinstance(params, dict) and isinstance(params.get("uri"), str):
                        with self._diagnostics_changed:
                            self._diagnostics[params["uri"]] = params
                            self._diagnostics_changed.notify_all()
        except (EOFError, ValueError, OSError) as exc:
            self._reader_error = exc
        finally:
            with self._lock:
                waiters = list(self._pending.values())
            for waiter in waiters:
                waiter.put({"error": {"message": "clangd connection closed"}})

    def request(self, method: str, params: dict[str, Any]) -> Any:
        if self.process is None or self.process.stdin is None:
            raise RuntimeError("clangd is not running")
        waiter: queue.Queue[dict] = queue.Queue()
        with self._lock:
            self._next_id += 1
            request_id = self._next_id
            self._pending[request_id] = waiter
        try:
            write_message(self.process.stdin, {
                "jsonrpc": "2.0", "id": request_id, "method": method, "params": params,
            })
            try:
                response = waiter.get(timeout=self.timeout)
            except queue.Empty as exc:
                raise TimeoutError(f"clangd request timed out: {method}") from exc
            if "error" in response:
                raise RuntimeError(f"clangd {method}: {response['error']}")
            return response.get("result")
        finally:
            with self._lock:
                self._pending.pop(request_id, None)

    def notify(self, method: str, params: dict[str, Any]) -> None:
        if self.process is None or self.process.stdin is None:
            raise RuntimeError("clangd is not running")
        write_message(self.process.stdin, {
            "jsonrpc": "2.0", "method": method, "params": params,
        })

    def open_document(self, path: Path, text: str, version: int = 1) -> str:
        path = path.resolve()
        if not path.is_relative_to(self.root):
            raise ValueError("document escapes workspace")
        uri = path.as_uri()
        self.notify("textDocument/didOpen", {"textDocument": {
            "uri": uri, "languageId": "c", "version": version, "text": text,
        }})
        return uri

    def change_document(self, uri: str, text: str, version: int) -> None:
        self.notify("textDocument/didChange", {
            "textDocument": {"uri": uri, "version": version},
            "contentChanges": [{"text": text}],
        })

    def diagnostics(self, uri: str) -> dict | None:
        with self._lock:
            return self._diagnostics.get(uri)

    def wait_diagnostics(self, uri: str, version: int) -> dict:
        """Wait for version-tagged diagnostics; never accept stale or unversioned data."""
        deadline = time.monotonic() + self.timeout
        with self._diagnostics_changed:
            while True:
                report = self._diagnostics.get(uri)
                if report is not None and report.get("version") == version:
                    return report
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError("diagnostics_unavailable_or_stale")
                self._diagnostics_changed.wait(remaining)

    def formatting(self, uri: str, tab_size: int = 4) -> list[dict]:
        return self.request("textDocument/formatting", {
            "textDocument": {"uri": uri},
            "options": {"tabSize": tab_size, "insertSpaces": True},
        }) or []

    def close(self) -> None:
        process = self.process
        if process is None:
            return
        try:
            if process.poll() is None:
                try:
                    self.request("shutdown", {})
                    self.notify("exit", {})
                except (RuntimeError, TimeoutError, BrokenPipeError, OSError):
                    pass
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=2)
        finally:
            if process.stdin is not None:
                process.stdin.close()
            if process.stdout is not None:
                process.stdout.close()
            self.process = None
