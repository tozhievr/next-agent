const { contextBridge, ipcRenderer } = require("electron");
contextBridge.exposeInMainWorld("setup", {
  status: () => ipcRenderer.invoke("setup:request", { action: "status" }),
  choose: () => ipcRenderer.invoke("setup:request", { action: "choose" }),
  install: () => ipcRenderer.invoke("setup:request", { action: "install" }),
  close: () => ipcRenderer.invoke("setup:request", { action: "close" }),
  launch: () => ipcRenderer.invoke("setup:request", { action: "launch" }),
});
