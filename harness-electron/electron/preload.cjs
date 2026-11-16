const { contextBridge, ipcRenderer } = require("electron");
const invoke = (action, payload = {}) =>
  ipcRenderer.invoke("harness:request", { action, ...payload });
contextBridge.exposeInMainWorld("harness", {
  bootstrap: () => invoke("bootstrap"),
  chooseWorkspace: () => invoke("chooseWorkspace"),
  newSession: () => invoke("new"),
  restoreSession: (id) => invoke("restore", { id }),
  send: (prompt, readOnly) => invoke("send", { prompt, readOnly }),
  cancel: () => invoke("cancel"),
  snapshot: () => invoke("snapshot"),
  sessions: () => invoke("list"),
  tree: (path = "") => invoke("tree", { path }),
  read: (path) => invoke("read", { path }),
  history: () => invoke("history"),
  labels: () => invoke("labels"),
  settings: (values) => invoke("settings", { values }),
  window: (command) => invoke("window", { command }),
  selfTest: () => invoke("selfTest"),
});
