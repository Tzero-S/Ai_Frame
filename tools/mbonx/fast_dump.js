/*
 * fast_dump — 只扫大块堆区间，命中 ONNX 头就立刻分块吐出，尽量缩短驻留时间
 * 说明：样本带反调试，注入后存活时间有限，因此把"扫描 + 读取"压在一次 RPC 里完成。
 */
'use strict';

const PRODUCER = '70 79 74 6f 72 63 68'; // ascii "pytorch"
const MIN_RANGE = 16 * 1024 * 1024;       // 模型 38MB，只看 ≥16MB 的块
const CHUNK = 1024 * 1024;
const MAX_BYTES = 96 * 1024 * 1024;

const log = (m) => send({ type: 'log', msg: m });

function bigRanges() {
  const out = [];
  const seen = {};
  for (const prot of ['rw-', 'r--']) {
    try {
      for (const r of Process.enumerateRanges(prot)) {
        const k = r.base.toString();
        if (seen[k]) continue;
        seen[k] = true;
        if (r.size >= MIN_RANGE) out.push(r);
      }
    } catch (e) {}
  }
  return out;
}

rpc.exports.dump = function (maxHits) {
  const t0 = Date.now();
  const ranges = bigRanges();
  log('[fast] 大块区间 ' + ranges.length + ' 个（≥16MB）');

  const hits = [];
  for (const r of ranges) {
    if (hits.length >= (maxHits || 3)) break;
    try {
      const found = Memory.scanSync(r.base, r.size, PRODUCER);
      for (const f of found) {
        // 命中点前 2 字节应是 08 <ir_version>
        let ok = false;
        try {
          const b0 = f.address.sub(2).readU8();
          const b1 = f.address.sub(1).readU8();
          ok = b0 === 0x08 && b1 >= 1 && b1 <= 12;
        } catch (e) {}
        if (ok) {
          hits.push(f.address.sub(2));
          log('[fast] 命中模型头 @ ' + f.address.sub(2));
          if (hits.length >= (maxHits || 3)) break;
        }
      }
    } catch (e) {}
  }
  log('[fast] 扫描耗时 ' + (Date.now() - t0) + 'ms，命中 ' + hits.length + ' 个');

  hits.forEach((start, i) => {
    let off = 0;
    while (off < MAX_BYTES) {
      const len = Math.min(CHUNK, MAX_BYTES - off);
      let buf = null;
      try {
        buf = start.add(off).readByteArray(len);
      } catch (e) {
        break;
      }
      if (!buf) break;
      send({ type: 'chunk', id: i, off: off, len: len }, buf);
      off += len;
    }
    send({ type: 'done', id: i, total: off });
    log('[fast] hit ' + i + ' 读取 ' + off + ' 字节');
  });
  return hits.length;
};
