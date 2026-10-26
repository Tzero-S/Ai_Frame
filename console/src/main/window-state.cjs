'use strict';
// 窗口位置/大小记忆（产品外壳的细节）：下次启动还原上次的窗口几何。
//
// 之所以单独成模块：这段逻辑有一个真实场景必须处理——用户换了显示器、
// 或拔掉外接屏后，上次保存的坐标会落在屏幕外，窗口"启动但看不见"。
// 这里用「与任一显示器工作区有交集」判定，判定失败就丢弃坐标只保留尺寸，
// 让 Electron 把窗口放回主屏。抽出来便于单独测（依赖通过参数注入）。
const fs = require('fs');

const DEFAULT = { width: 1280, height: 820 };
const MIN = { width: 1100, height: 720 };

/**
 * 读取上次保存的窗口几何。
 * @param {{stateFile: string, screen?: {getAllDisplays(): Array<{workArea:{x:number,y:number,width:number,height:number}}>}}} opts
 * @returns {{width:number,height:number,x?:number,y?:number}}
 */
function loadBounds({ stateFile, screen }) {
  let s;
  try {
    s = JSON.parse(fs.readFileSync(stateFile, 'utf8'));
  } catch {
    return { width: DEFAULT.width, height: DEFAULT.height };
  }
  if (!s || typeof s !== 'object') return { width: DEFAULT.width, height: DEFAULT.height };

  const width = Math.max(MIN.width, Math.round(Number(s.width)) || DEFAULT.width);
  const height = Math.max(MIN.height, Math.round(Number(s.height)) || DEFAULT.height);
  const x = Number.isFinite(Number(s.x)) ? Math.round(Number(s.x)) : undefined;
  const y = Number.isFinite(Number(s.y)) ? Math.round(Number(s.y)) : undefined;
  if (x === undefined || y === undefined) return { width, height };

  // 没有 screen（单测/异常环境）就直接信坐标
  if (!screen || typeof screen.getAllDisplays !== 'function') return { width, height, x, y };

  const visible = screen.getAllDisplays().some((d) => {
    const a = d && d.workArea;
    if (!a) return false;
    return x < a.x + a.width && x + width > a.x && y < a.y + a.height && y + height > a.y;
  });
  return visible ? { width, height, x, y } : { width, height };
}

/** 原子写（先写 .tmp 再 rename），避免断电/强杀留下半个 JSON。 */
function saveBounds({ stateFile, bounds }) {
  if (!bounds) return false;
  try {
    const tmp = stateFile + '.tmp';
    fs.writeFileSync(tmp, JSON.stringify(bounds));
    fs.renameSync(tmp, stateFile);
    return true;
  } catch {
    return false; // 写失败不影响使用
  }
}

module.exports = { loadBounds, saveBounds, DEFAULT, MIN };
