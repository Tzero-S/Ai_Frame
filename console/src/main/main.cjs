'use strict';
// Electron 主进程（§4.2）：产品外壳 = 窗口 + 托盘 + 引擎进程托管 + 命名管道 IPC。
const { app, BrowserWindow, ipcMain, Menu, Tray, nativeImage } = require('electron');
const path = require('path');
const { AimPipeClient } = require('./ipc-client.cjs');
const { EngineHost } = require('./engine-host.cjs');

// 若 ELECTRON_RUN_AS_NODE=1，Electron 会退化成纯 Node，主进程 API 全部缺失。
if (!app || !ipcMain) {
  console.error('[main] Electron 主进程 API 不可用（app/ipcMain 为 undefined）。\n' +
                '       常见原因：环境变量 ELECTRON_RUN_AS_NODE=1。\n' +
                '       请改用 `npm run electron` 启动（启动器会清理该变量）。');
  process.exit(1);
}

const PRODUCT = 'AiFrame 控制台';
app.setName('AiFrame');
app.setAppUserModelId('com.aimframe.console');

// ---------------------------------------------------------------- 单实例
// 第二个实例直接退出并把已有窗口叫到前台，避免两个控制台抢同一个引擎。
const gotLock = app.requestSingleInstanceLock();
if (!gotLock) {
  app.quit();
}

// ---------------------------------------------------------------- 引擎托管
const host = new EngineHost();
let client = null;
let win = null;
let tray = null;
let quitting = false;

// 管道名解析：环境变量 → 引擎托管记录的 → 发现文件 → 旧默认名
function currentPipe() {
  if (process.env.AIM_PIPE) return process.env.AIM_PIPE;
  if (host.pipe) return host.pipe;
  const d = host.readDiscovery();
  return d || 'aim_engine';
}

async function ensureClient() {
  if (client) return client;
  const c = new AimPipeClient(currentPipe(), 1500);
  await c.connect();
  client = c;
  return c;
}

function dropClient() {
  if (client) {
    try {
      client.close();
    } catch {
      /* ignore */
    }
    client = null;
  }
}

// ---------------------------------------------------------------- IPC
ipcMain.handle('aim:pipe', () => currentPipe());

// 屏幕信息：必须给**物理像素**。渲染进程的 window.screen.width 是 CSS 像素
// （已除以 devicePixelRatio），在 125% 缩放的 1920x1080 上会得到 1536x864，
// 拿它当"当前分辨率"显示就是错的。这里统一用 size * scaleFactor。
ipcMain.handle('aim:screen', () => {
  const { screen } = require('electron');
  const displays = screen.getAllDisplays().map((d, i) => ({
    index: i,
    id: d.id,
    primary: d.id === screen.getPrimaryDisplay().id,
    width: Math.round(d.size.width * d.scaleFactor),
    height: Math.round(d.size.height * d.scaleFactor),
    cssWidth: d.size.width,
    cssHeight: d.size.height,
    scaleFactor: d.scaleFactor,
    bounds: d.bounds,
  }));
  const p = screen.getPrimaryDisplay();
  return {
    // 主屏物理分辨率（引擎 GetPrimaryResolution 同口径）
    width: Math.round(p.size.width * p.scaleFactor),
    height: Math.round(p.size.height * p.scaleFactor),
    scaleFactor: p.scaleFactor,
    displays,
  };
});

ipcMain.handle('aim:invoke', async (_evt, cmd, payload) => {
  try {
    const c = await ensureClient();
    const response = await c.invoke(cmd, payload ?? {});
    return { ok: true, response };
  } catch (e) {
    dropClient();
    return { ok: false, error: String(e && e.message ? e.message : e) };
  }
});

ipcMain.handle('aim:engine', () => host.status());
ipcMain.handle('aim:engine-start', async () => {
  dropClient();
  const st = await host.start();
  refreshTray();
  return st;
});
ipcMain.handle('aim:engine-stop', async () => {
  dropClient();
  const st = await host.stop();
  refreshTray();
  return st;
});
ipcMain.handle('aim:engine-restart', async () => {
  dropClient();
  const st = await host.restart();
  refreshTray();
  return st;
});

// ---------------------------------------------------------------- 窗口
function iconPath() {
  return path.join(__dirname, '../../build/icon.png');
}

// 窗口几何记忆：实现与边界判定见 window-state.cjs（换显示器时不会把窗口丢到屏幕外）
const { loadBounds: loadBoundsOf, saveBounds: saveBoundsTo } = require('./window-state.cjs');

function stateFile() {
  return path.join(app.getPath('userData'), 'window-state.json');
}

function loadBounds() {
  return loadBoundsOf({ stateFile: stateFile(), screen: require('electron').screen });
}

let saveTimer = null;
function saveBounds(w) {
  if (!w || w.isDestroyed() || w.isMinimized() || w.isFullScreen()) return;
  clearTimeout(saveTimer);
  // 拖动/缩放会连续触发，防抖到静止后再落盘
  saveTimer = setTimeout(() => {
    const b = w.getNormalBounds ? w.getNormalBounds() : w.getBounds();
    saveBoundsTo({ stateFile: stateFile(), bounds: b });
  }, 400);
}

function createWindow() {
  win = new BrowserWindow({
    ...loadBounds(),
    minWidth: 1100,
    minHeight: 720,
    backgroundColor: '#F5F7FA',
    show: false,
    title: PRODUCT,
    icon: iconPath(),
    autoHideMenuBar: true,
    webPreferences: {
      preload: path.join(__dirname, 'preload.cjs'),
      contextIsolation: true,
      nodeIntegration: false,
    },
  });

  const dev = process.env.VITE_DEV_SERVER_URL;
  if (dev) win.loadURL(dev);
  else win.loadFile(path.join(__dirname, '../../dist/index.html'));

  // 页面 <title> 不覆盖窗口标题（统一由 PRODUCT 决定，避免托盘/任务栏出现旧名）
  win.on('page-title-updated', (e) => e.preventDefault());

  win.once('ready-to-show', () => win.show());

  win.on('resize', () => saveBounds(win));
  win.on('move', () => saveBounds(win));

  // 关闭 → 收进托盘（真正的退出走托盘菜单/菜单栏退出）
  win.on('close', (e) => {
    saveBounds(win);
    if (quitting) return;
    e.preventDefault();
    win.hide();
  });
  win.on('closed', () => {
    win = null;
  });
}

function showWindow() {
  if (!win) createWindow();
  if (!win) return;
  if (win.isMinimized()) win.restore();
  win.show();
  win.focus();
}

// ---------------------------------------------------------------- 托盘
function refreshTray() {
  if (!tray) return;
  const st = host.status();
  const label = st.running
    ? `引擎：运行中${st.external ? '（外部）' : ''}${st.pipe ? ' · ' + st.pipe : ''}`
    : st.error
      ? `引擎：未运行（${st.error}）`
      : '引擎：未运行';
  tray.setToolTip(`${PRODUCT}\n${label}`);
  tray.setContextMenu(
    Menu.buildFromTemplate([
      { label: PRODUCT, enabled: false },
      { type: 'separator' },
      { label, enabled: false },
      {
        label: '启动引擎',
        enabled: !st.running,
        click: async () => {
          await host.start();
          dropClient();
          refreshTray();
        },
      },
      {
        label: '停止引擎',
        enabled: st.running,
        click: async () => {
          await host.stop();
          dropClient();
          refreshTray();
        },
      },
      { type: 'separator' },
      { label: '显示控制台', click: showWindow },
      {
        label: '退出',
        click: () => {
          quitting = true;
          app.quit();
        },
      },
    ]),
  );
}

function createTray() {
  const img = nativeImage.createFromPath(iconPath());
  tray = new Tray(img.isEmpty() ? nativeImage.createEmpty() : img.resize({ width: 16, height: 16 }));
  tray.on('click', showWindow);
  refreshTray();
}

// ---------------------------------------------------------------- 生命周期
app.on('second-instance', () => {
  showWindow();
});

app.whenReady().then(async () => {
  Menu.setApplicationMenu(null);
  createWindow();
  createTray();

  // 自动拉起引擎（已有实例则复用，不会重复启动）
  const st = await host.start();
  refreshTray();
  if (!st.running) {
    // 不阻断启动：窗口照常显示，状态在「诊断」页可见
    console.warn('[main] 引擎未就绪：', st.error || '未知原因');
  }

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
  });
});

// 所有窗口关闭时不退出（缩到托盘）；真正退出由 quitting 控制
app.on('window-all-closed', () => {
  if (quitting && process.platform !== 'darwin') app.quit();
});

// 退出清理：before-quit 的异步回调不会被框架等待（旧实现里 await 是空转，
// 进程可能在 taskkill 之前就退了，留下孤儿引擎）。改成两段式：
//   第一次 before-quit → 拦下退出，异步停引擎，完成后再 app.quit()；
//   第二次 before-quit → 直接放行。
let quittingCleanupDone = false;
let quittingCleanupRunning = false;

app.on('before-quit', (e) => {
  quitting = true;
  if (quittingCleanupDone) return; // 第二次进来：放行
  const managed = host.status().managed;
  if (!managed) {
    quittingCleanupDone = true;
    return;
  }
  if (quittingCleanupRunning) {
    e.preventDefault(); // 清理进行中，继续等
    return;
  }
  e.preventDefault();
  quittingCleanupRunning = true;
  void host
    .stop()
    .catch(() => {})
    .finally(() => {
      quittingCleanupRunning = false;
      quittingCleanupDone = true;
      app.quit();
    });
});
