'use strict';
// 命名管道客户端（§8.1）：4 字节小端长度前缀 + JSON 帧。
const net = require('net');

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
    this.socket.on('data', (chunk) => {
      this.buffer = Buffer.concat([this.buffer, chunk]);
      while (this.buffer.length >= 4) {
        const len = this.buffer.readUInt32LE(0);
        if (this.buffer.length < 4 + len) break;
        const body = this.buffer.subarray(4, 4 + len).toString('utf8');
        this.buffer = this.buffer.subarray(4 + len);
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
