/*
 * mbonx scan agent — 对已运行的样本做内存扫描，抓取已解密的 ONNX 明文
 *
 * 适用场景：样本已经把模型加载进 ORT（会话已创建），钩子挂得太晚。
 * 原理：ONNX 文件头是 protobuf，第二个字段固定为 producer_name="pytorch"，
 *       字节特征 12 07 70 79 74 6F 72 63 68；命中后向前退 2 字节即为模型起点
 *       （08 <ir_version>）。
 * 读取策略：从起点按 1MB 分块读，遇到不可读页即停（模型是连续堆块）。
 */
'use strict';

const SIG = [0x12, 0x07, 0x70, 0x79, 0x74, 0x6f, 0x72, 0x63, 0x68]; // \x12\x07pytorch
const CHUNK = 1024 * 1024;
const MAX_BYTES = 128 * 1024 * 1024;
const log = (m) => send({ type: 'log', msg: m });

function findAll(maxHits) {
  const hits = [];
  // 明文在堆上（rw-），只扫 r-- 会漏
  const ranges = [];
  for (const prot of ['rw-', 'r--']) {
    try {
      for (const r of Process.enumerateRanges(prot)) {
        if (r.size >= 2 * 1024 * 1024) ranges.push(r);
      }
    } catch (e) {}
  }
  log('[scan] 可读区间 ' + ranges.length + ' 个');
  for (const r of ranges) {
    if (hits.length >= maxHits) break;
    if (r.size < 2 * 1024 * 1024) continue; // 模型不可能在 <2MB 的块里
    try {
      let cur = r.base;
      const end = r.base.add(r.size);
      while (cur.compare(end) < 0 && hits.length < maxHits) {
        const found = Memory.scanSync(cur, end.sub(cur), SIG);
        if (!found || found.length === 0) break;
        const a = found[0].address;
        hits.push(a);
        cur = a.add(1);
      }
    } catch (e) {
      /* 区间不可扫，跳过 */
    }
  }
  return hits;
}

rpc.exports.scananddump = function (maxHits) {
  const hits = findAll(maxHits || 4);
  log('[scan] 命中 ' + hits.length + ' 处 ONNX 特征');
  hits.forEach((h, i) => {
    const start = h.sub(2); // 08 <ir_version>
    log('[hit ' + i + '] ' + h + ' → 起点 ' + start);
    let off = 0;
    let total = 0;
    while (off < MAX_BYTES) {
      const len = Math.min(CHUNK, MAX_BYTES - off);
      let buf = null;
      try {
        buf = start.add(off).readByteArray(len);
      } catch (e) {
        log('[hit ' + i + '] 在 ' + off + ' 处不可读，停止');
        break;
      }
      if (!buf) break;
      send({ type: 'chunk', id: i, off: off, len: len }, buf);
      off += len;
      total += len;
    }
    send({ type: 'done', id: i, total: total });
    log('[hit ' + i + '] 已读取 ' + total + ' 字节');
  });
  return hits.length;
};
