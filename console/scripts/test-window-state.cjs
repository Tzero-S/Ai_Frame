'use strict';
// window-state 单测：不依赖 Electron（screen 通过参数注入）。
// 覆盖真实会遇到的情况：首次启动、状态文件损坏、尺寸被改小、换显示器后坐标跑到屏幕外。
const fs = require('fs');
const os = require('os');
const path = require('path');
const assert = require('assert');
const { loadBounds, saveBounds, DEFAULT, MIN } = require('../src/main/window-state.cjs');

const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'aimframe-ws-'));
const file = path.join(dir, 'window-state.json');
let n = 0;
const ok = (name) => console.log(`  ok ${++n} - ${name}`);

// 假显示器：主屏 1920×1080（工作区去掉任务栏 40px），副屏在左边
const screen = {
  getAllDisplays: () => [
    { workArea: { x: 0, y: 0, width: 1920, height: 1040 } },
    { workArea: { x: -1280, y: 0, width: 1280, height: 1024 } },
  ],
};

// 1. 没有状态文件 → 默认尺寸，不带坐标
assert.deepStrictEqual(loadBounds({ stateFile: file, screen }), {
  width: DEFAULT.width,
  height: DEFAULT.height,
});
ok('首次启动回退默认尺寸');

// 2. 文件损坏 → 默认，不抛异常
fs.writeFileSync(file, '{ not json');
assert.deepStrictEqual(loadBounds({ stateFile: file, screen }), {
  width: DEFAULT.width,
  height: DEFAULT.height,
});
ok('状态文件损坏时回退默认值');

// 3. 尺寸小于最小值 → 抬到最小值
fs.writeFileSync(file, JSON.stringify({ width: 400, height: 300, x: 100, y: 100 }));
const clamped = loadBounds({ stateFile: file, screen });
assert.strictEqual(clamped.width, MIN.width);
assert.strictEqual(clamped.height, MIN.height);
assert.strictEqual(clamped.x, 100);
ok('过小尺寸被抬到最小尺寸，坐标保留');

// 4. 完全在主屏内 → 原样还原
fs.writeFileSync(file, JSON.stringify({ width: 1280, height: 820, x: 200, y: 120 }));
assert.deepStrictEqual(loadBounds({ stateFile: file, screen }), {
  width: 1280,
  height: 820,
  x: 200,
  y: 120,
});
ok('屏幕内坐标原样还原');

// 5. 坐标跑到屏幕外（拔掉外接屏的典型情况）→ 丢坐标只留尺寸
fs.writeFileSync(file, JSON.stringify({ width: 1280, height: 820, x: 5000, y: 3000 }));
assert.deepStrictEqual(loadBounds({ stateFile: file, screen }), { width: 1280, height: 820 });
ok('屏幕外坐标被丢弃，窗口回到主屏');

// 6. 负坐标但落在左侧副屏 → 保留（不能一律把负数当非法）
fs.writeFileSync(file, JSON.stringify({ width: 1100, height: 720, x: -1200, y: 50 }));
assert.deepStrictEqual(loadBounds({ stateFile: file, screen }), {
  width: 1100,
  height: 720,
  x: -1200,
  y: 50,
});
ok('副屏负坐标正常还原');

// 7. 只有部分重叠（窗口一半在屏外）→ 仍算可见
fs.writeFileSync(file, JSON.stringify({ width: 1280, height: 820, x: 1500, y: 800 }));
const partial = loadBounds({ stateFile: file, screen });
assert.strictEqual(partial.x, 1500);
assert.strictEqual(partial.y, 800);
ok('部分重叠视为可见');

// 8. 写入 → 读回一致，且不留 .tmp
assert.strictEqual(saveBounds({ stateFile: file, bounds: { width: 1200, height: 760, x: 10, y: 20 } }), true);
assert.strictEqual(fs.existsSync(file + '.tmp'), false);
assert.deepStrictEqual(loadBounds({ stateFile: file, screen }), {
  width: 1200,
  height: 760,
  x: 10,
  y: 20,
});
ok('保存后可原样读回，无 .tmp 残留');

// 9. 写失败不抛（目录不存在）
assert.strictEqual(saveBounds({ stateFile: path.join(dir, 'nope', 'x.json'), bounds: { width: 1 } }), false);
ok('写失败返回 false 不抛异常');

// 10. 无 screen 注入（异常环境）→ 直接信坐标
fs.writeFileSync(file, JSON.stringify({ width: 1280, height: 820, x: 5000, y: 3000 }));
assert.deepStrictEqual(loadBounds({ stateFile: file }), {
  width: 1280,
  height: 820,
  x: 5000,
  y: 3000,
});
ok('无 screen 时不做可见性判定');

fs.rmSync(dir, { recursive: true, force: true });
console.log(`window-state: ${n}/${n} 通过`);
