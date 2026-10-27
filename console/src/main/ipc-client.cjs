'use strict';
// 命名管道客户端（§8.1）：4 字节小端长度前缀 + JSON 帧。
const net = require('net');

// 与 engine/src/ipc/framing.h 的 kMaxFrameBytes 保持一致
const MAX_FRAME_BYTES = 16 * 1024 * 1024;

function encodeFrame(payload) {
  const body = Buffer.from(payload, 'utf8');
  const head = Buffer.alloc(4);
  head.writeUInt32LE(body.length, 0);
  return Buffer.concat([head, body]);
}

class AimPipeClient {
  constructor(pipeName = 'aim_engine', timeoutMs = 2000) {
    this.pipeName = pipeName;
    this.timeoutMs = timeoutMs;
    this.socket = null;
    this.buffer = Buffer.alloc(0);
    this.chunks = [];
    this.pending = new Map();
    this.seq = 0;
  }

  connect() {
    return new Promise((resolve, reject) => {
      const path = `\\\\.\\pipe\\${this.pipeName}`;
      const sock = net.connect(path);
      const timer = setTimeout(() => {
        sock.destroy();
        reject(new Error('connect timeout'));
      }, this.timeoutMs);

      sock.once('connect', () => {
        clearTimeout(timer);
        this.socket = sock;
        this._bind();
        resolve();
      });
      sock.once('error', (e) => {
        clearTimeout(timer);
        reject(e);
      });
    });
  }

  _bind() {
    // 收包缓冲：把到达的 chunk 先压进数组，只在需要解析时拼一次。
    // 旧实现对每个 data 事件都 Buffer.concat 全量重拼，大响应（模型预览几百 KB）
    // 会退化成 O(n²) 拷贝。
    this.socket.on('data', (chunk) => {
      this.chunks.push(chunk);
      this.buffer = this.chunks.length === 1 ? this.chunks[0] : Buffer.concat(this.chunks);
      if (this.chunks.length > 1) this.chunks = [this.buffer];
      while (this.buffer.length >= 4) {
        const len = this.buffer.readUInt32LE(0);
        if (len > MAX_FRAME_BYTES) {
          // 与引擎侧 kMaxFrameBytes 对齐：畸形长度直接断连，而不是永久等下去
          this.socket.destroy();
          return;
        }
        if (this.buffer.length < 4 + len) break;
        const body = this.buffer.subarray(4, 4 + len).toString('utf8');
        this.buffer = this.buffer.subarray(4 + len);
        this.chunks = [this.buffer];
        let msg;
        try {
          msg = JSON.parse(body);
        } catch {
          continue;
        }
        const p = this.pending.get(msg.id);
        if (p) {
          this.pending.delete(msg.id);
          p.resolve(msg);
        }
      }
    });

    this.socket.on('close', () => {
      for (const [, p] of this.pending) p.reject(new Error('pipe closed'));
      this.pending.clear();
    });
    this.socket.on('error', () => {});
  }

  invoke(cmd, payload = {}) {
    if (!this.socket) return Promise.reject(new Error('not connected'));
    const id = `req-${++this.seq}`;
    const req = { id, cmd, ts: Date.now(), payload };

    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error('invoke timeout'));
      }, this.timeoutMs);

      this.pending.set(id, {
        resolve: (m) => {
          clearTimeout(timer);
          resolve(m);
        },
        reject: (e) => {
          clearTimeout(timer);
          reject(e);
        },
      });

      this.socket.write(encodeFrame(JSON.stringify(req)));
    });
  }

  close() {
    if (this.socket) {
      this.socket.destroy();
      this.socket = null;
    }
  }
}

module.exports = { AimPipeClient, encodeFrame };
