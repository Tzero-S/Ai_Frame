'use strict';
// 引擎进程托管（产品外壳的核心）：启动 / 停止 / 健康探测。
//
// 设计要点：
//   1) 先探测是否已有引擎在跑（读发现文件 → 连一次 hello）。有就复用，绝不重复拉起。
//   2) 没有才 spawn，并且先删掉陈旧的发现文件，避免读到上一次的管道名。
//   3) 只杀自己拉起的进程；外部启动的引擎不碰（避免误杀用户自己的实例）。
const { spawn } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { AimPipeClient } = require('./ipc-client.cjs');

const DISCOVERY = path.join(os.tmpdir(), 'aimframe.pipe');

class EngineHost {
  constructor() {
    this.child = null;
    this.pipe = '';
    this.error = '';
    this.startedAt = 0;
    this.external = false; // true = 复用已有实例，不由本进程管理生命周期
  }

  // 引擎可执行文件：优先环境变量，其次开发目录，最后与安装目录同级
  exePath() {
    if (process.env.AIM_ENGINE_EXE) return process.env.AIM_ENGINE_EXE;
    const dev = path.resolve(__dirname, '../../../engine/build-mingw/src/helper.exe');
    if (fs.existsSync(dev)) return dev;
    const shipped = path.resolve(process.resourcesPath || __dirname, 'engine/helper.exe');
    return shipped;
  }

  readDiscovery() {
    try {
      const name = fs.readFileSync(DISCOVERY, 'utf8').trim();
      return name || '';
    } catch {
      return '';
    }
  }

  // 能否连上某个管道名（1.2s 内）
  async canConnect(pipe) {
    if (!pipe) return false;
    const c = new AimPipeClient(pipe, 1200);
    try {
      await c.connect();
      await c.invoke('engine.hello');
      c.close();
      return true;
    } catch {
      try {
        c.close();
      } catch {
        /* ignore */
      }
      return false;
    }
  }

  status() {
    return {
      managed: !!this.child,
      external: this.external,
      running: !!(this.child || this.external),
      pid: this.child ? this.child.pid : 0,
      pipe: this.pipe,
      exe: this.exePath(),
      exeExists: fs.existsSync(this.exePath()),
      uptimeMs: this.startedAt ? Date.now() - this.startedAt : 0,
      error: this.error,
    };
  }

  async start() {
    if (this.child || this.external) return this.status();

    // 1) 已有实例 → 复用
    const existing = this.readDiscovery();
    if (existing && (await this.canConnect(existing))) {
      this.pipe = existing;
      this.external = true;
      this.startedAt = Date.now();
      this.error = '';
      return this.status();
    }

    // 2) 拉起新实例
    const exe = this.exePath();
    if (!fs.existsSync(exe)) {
      this.error = `引擎可执行文件不存在：${exe}（可用 AIM_ENGINE_EXE 指定）`;
      return this.status();
    }

    // 清掉陈旧发现文件，避免读到上一次的管道名
    try {
      fs.unlinkSync(DISCOVERY);
    } catch {
      /* 不存在就算了 */
    }

    this.error = '';
    // stdin 必须保持打开：`--serve` 以 getchar() 作为"等待退出"的阻塞点，
    // 若用 stdio:'ignore'（stdin 指向 /dev/null），getchar 立即返回 EOF → 引擎秒退。
    // 这里给一根永远不写的管道，进程存活期与父进程一致。
    this.child = spawn(exe, ['--serve'], {
      windowsHide: true,
      stdio: ['pipe', 'ignore', 'ignore'],
      detached: false,
    });
    this.startedAt = Date.now();

    this.child.on('exit', () => {
      this.child = null;
      this.pipe = '';
    });
    this.child.on('error', (e) => {
      this.error = String(e && e.message ? e.message : e);
      this.child = null;
    });

    // 3) 等发现文件出现并可连（最多 10s）
    const deadline = Date.now() + 10000;
    while (Date.now() < deadline) {
      const name = this.readDiscovery();
      if (name && (await this.canConnect(name))) {
        this.pipe = name;
        return this.status();
      }
      await new Promise((r) => setTimeout(r, 250));
    }
    this.error = '引擎启动超时（10s 内未就绪）';
    return this.status();
  }

  async stop() {
    if (!this.child) {
      // 外部实例：只断开引用，不杀进程
      this.external = false;
      this.pipe = '';
      this.startedAt = 0;
      return this.status();
    }
    const pid = this.child.pid;
    try {
      this.child.kill();
    } catch {
      /* 已退出 */
    }
    // 兜底：Windows 上确认进程真的没了
    if (process.platform === 'win32' && pid) {
      await new Promise((r) => setTimeout(r, 300));
      try {
        const { execFileSync } = require('child_process');
        execFileSync('taskkill', ['/PID', String(pid), '/T', '/F'], { stdio: 'ignore' });
      } catch {
        /* 已退出即报错，忽略 */
      }
    }
    this.child = null;
    this.pipe = '';
    this.startedAt = 0;
    this.error = '';
    return this.status();
  }

  async restart() {
    await this.stop();
    return this.start();
  }
}

module.exports = { EngineHost, DISCOVERY };
