/*
 * 诊断扫描：统计若干特征在可读内存中的命中数，定位明文/密文的实际形态
 * 注意：模型明文落在堆上（rw-），只扫 r-- 会全部漏掉；
 *       另外 Memory.scanSync 的 size 必须传 number，UInt64 会抛异常。
 */
'use strict';

const PATTERNS = [
  { name: 'ascii:pytorch', hex: '70 79 74 6f 72 63 68' },
  { name: 'ascii:tf2onnx', hex: '74 66 32 6f 6e 6e 78' },
  { name: 'ascii:onnx', hex: '6f 6e 6e 78' },
  { name: 'ascii:PDAIMBEN', hex: '50 44 41 49 4d 42 45 4e' },
  { name: 'pb:ir7+producer', hex: '08 07 12' },
  { name: 'pb:ir8+producer', hex: '08 08 12' },
  { name: 'ascii:mbonx', hex: '6d 62 6f 6e 78' },
];

const log = (m) => send({ type: 'log', msg: m });

function readableRanges(minSize) {
  const out = [];
  const seen = {};
  for (const prot of ['rw-', 'r--', 'rwx']) {
    try {
      for (const r of Process.enumerateRanges(prot)) {
        const key = r.base.toString();
        if (seen[key]) continue;
        seen[key] = true;
        if (r.size >= (minSize || 64 * 1024)) out.push(r);
      }
    } catch (e) {}
  }
  return out;
}

rpc.exports.diag = function () {
  const ranges = readableRanges(64 * 1024);
  log('[diag] 扫描区间 ' + ranges.length + ' 个');
  const summary = {};
  for (const p of PATTERNS) {
    let count = 0;
    const samples = [];
    for (const r of ranges) {
      try {
        let cur = r.base;
        const end = r.base.add(r.size);
        let guard = 0;
        while (cur.compare(end) < 0 && guard < 6) {
          const remain = end.sub(cur).toNumber();
          if (remain <= 0) break;
          const found = Memory.scanSync(cur, remain, p.hex);
          if (!found || found.length === 0) break;
          count += found.length;
          if (samples.length < 3) samples.push(found[0].address.toString());
          cur = found[found.length - 1].address.add(1);
          guard++;
        }
      } catch (e) {}
    }
    summary[p.name] = { count: count, samples: samples };
    log('[diag] ' + p.name + ' → ' + count + (samples.length ? '  e.g. ' + samples.join(', ') : ''));
  }
  return summary;
};

rpc.exports.ranges = function () {
  return readableRanges(1024 * 1024).map((r) => ({ base: r.base.toString(), size: r.size }));
};
