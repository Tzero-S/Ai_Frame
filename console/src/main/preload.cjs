'use strict';
const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('aim', {
  invoke: (cmd, payload) => ipcRenderer.invoke('aim:invoke', cmd, payload),
  getPipeName: () => ipcRenderer.invoke('aim:pipe'),
  // 屏幕物理分辨率（渲染进程的 window.screen 是 CSS 像素，不能直接用）
  screenInfo: () => ipcRenderer.invoke('aim:screen'),
  // 产品外壳：引擎进程托管（启动 / 停止 / 重启 / 状态）
  engine: {
    status: () => ipcRenderer.invoke('aim:engine'),
    start: () => ipcRenderer.invoke('aim:engine-start'),
    stop: () => ipcRenderer.invoke('aim:engine-stop'),
    restart: () => ipcRenderer.invoke('aim:engine-restart'),
  },
});
