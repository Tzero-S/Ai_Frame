'use strict';
// IPC 客户端冒烟：本地命名管道 mock server ↔ AimPipeClient 往返。
const net = require('net');
const { AimPipeClient, encodeFrame } = require('../src/main/ipc-client.cjs');

const pipeName = `aim_console_smoke_${process.pid}`;
const pipePath = `\\\\.\\pipe\\${pipeName}`;

function startServer() {
  return new Promise((resolve, reject) => {
    const server = net.createServer((sock) => {
      let buf = Buffer.alloc(0);
      sock.on('data', (chunk) => {
        buf = Buffer.concat([buf, chunk]);
        while (buf.length >= 4) {
          const len = buf.readUInt32LE(0);
          if (buf.length < 4 + len) break;
          const body = buf.subarray(4, 4 + len).toString('utf8');
          buf = buf.subarray(4 + len);
          const req = JSON.parse(body);
          const resp = {
            id: req.id,
            code: 0,
            msg: 'ok',
            payload: { echo: req.cmd, n: 42 },
          };
          sock.write(encodeFrame(JSON.stringify(resp)));
        }
      });
      sock.on('error', () => {});
    });
    server.once('error', reject);
    server.listen(pipePath, () => resolve(server));
  });
}

(async () => {
  const server = await startServer();
  const client = new AimPipeClient(pipeName, 2000);
  try {
    await client.connect();
    const resp = await client.invoke('engine.hello', { x: 1 });
    if (resp.code !== 0 || resp.payload.echo !== 'engine.hello' || resp.payload.n !== 42) {
      throw new Error('unexpected response: ' + JSON.stringify(resp));
    }
    console.log('IPC smoke OK:', JSON.stringify(resp));
    client.close();
    server.close();
    process.exit(0);
  } catch (e) {
    console.error('IPC smoke FAIL:', e.message);
    client.close();
    server.close();
    process.exit(1);
  }
})();
