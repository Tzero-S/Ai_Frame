'use strict';
// Electron 启动器：清掉宿主/CI 注入的 ELECTRON_RUN_AS_NODE 与 NODE_OPTIONS 后再拉起 Electron。
// 背景：若 ELECTRON_RUN_AS_NODE=1，`electron .` 会退化为纯 Node 进程，
// require('electron') 只返回二进制路径，main 进程里的 app/ipcMain 全为 undefined。
const path = require('path');
const { spawn } = require('child_process');

const electronBinary = require('electron'); // 纯 Node 下返回可执行文件路径
if (typeof electronBinary !== 'string') {
  console.error('[launch] 未找到 Electron 二进制，请先执行：npm run postinstall（或 npm install）');
  process.exit(1);
}

const env = { ...process.env };
delete env.ELECTRON_RUN_AS_NODE;
delete env.ELECTRON_NO_ATTACH_CONSOLE;
// NODE_OPTIONS 里的 --require shim 会被 Electron 继承，可能导致主进程注入失败
delete env.NODE_OPTIONS;

const appDir = path.join(__dirname, '..');
const child = spawn(electronBinary, ['.'], { cwd: appDir, env, stdio: 'inherit' });
child.on('exit', (code) => process.exit(code == null ? 0 : code));
child.on('error', (e) => {
  console.error('[launch] 启动 Electron 失败:', e.message);
  process.exit(1);
});
