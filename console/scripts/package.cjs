'use strict';
// 便携版打包（产品外壳的出厂形态）：把 Electron 运行时 + 渲染产物 + 引擎装成一个可直接运行的目录/压缩包。
//
// 为什么不用 electron-builder：
//   它的 NSIS 安装器要在构建期从 GitHub 拉二进制，本机网络到 GitHub 不通（vcpkg 克隆已证实）。
//   便携目录 + zip 是零外部依赖、可复现的路径，且用户解压即用、不写注册表。
//   若后续要安装器，装好 electron-builder 并保留同一套 resources 布局即可（engine-host.cjs
//   的 exePath() 已按 process.resourcesPath/engine/helper.exe 约定解析）。
//
// 产物布局：
//   dist-pkg/AiFrame/
//     AiFrame.exe                  ← Electron 运行时改名
//     resources/app/               ← 控制台（main/preload/renderer）
//     resources/engine/            ← 引擎 + onnxruntime.dll + data/patterns.json
//   dist-pkg/AiFrame-<ver>-win-x64.zip
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

const ROOT = path.resolve(__dirname, '..');            // console/
const REPO = path.resolve(ROOT, '..');                 // Ai_Frame/
const OUT = path.join(ROOT, 'dist-pkg');
const APP = path.join(OUT, 'AiFrame');
const ELECTRON = path.join(ROOT, 'node_modules/electron/dist');

const pkg = JSON.parse(fs.readFileSync(path.join(ROOT, 'package.json'), 'utf8'));
const VER = pkg.version || '1.0.0';

const log = (...a) => console.log('[package]', ...a);

function die(msg) {
  console.error('[package] 失败：' + msg);
  process.exit(1);
}

function copyDir(src, dst, filter) {
  fs.mkdirSync(dst, { recursive: true });
  for (const ent of fs.readdirSync(src, { withFileTypes: true })) {
    const s = path.join(src, ent.name);
    const d = path.join(dst, ent.name);
    if (filter && !filter(s, ent)) continue;
    if (ent.isDirectory()) copyDir(s, d, filter);
    else if (ent.isFile()) fs.copyFileSync(s, d);
  }
}

function copyFile(src, dst) {
  if (!fs.existsSync(src)) die(`缺少文件：${src}`);
  fs.mkdirSync(path.dirname(dst), { recursive: true });
  fs.copyFileSync(src, dst);
}

// ---- 0. 前置检查 ----
if (!fs.existsSync(ELECTRON)) die(`Electron 运行时不存在：${ELECTRON}（先 npm install）`);
if (!fs.existsSync(path.join(ROOT, 'dist/index.html'))) {
  die('渲染产物不存在：先 npm run build');
}
const ENGINE_SRC = path.join(REPO, 'engine/build-mingw/src/helper.exe');
if (!fs.existsSync(ENGINE_SRC)) {
  die(`引擎不存在：${ENGINE_SRC}（先构建引擎，或用 AIM_ENGINE_EXE 指定的构建目录）`);
}
const ORT_DLL = path.join(REPO, 'engine/build-mingw/src/onnxruntime.dll');

// ---- 1. 清空输出 ----
fs.rmSync(OUT, { recursive: true, force: true });
fs.mkdirSync(APP, { recursive: true });

// ---- 2. Electron 运行时 ----
log('复制 Electron 运行时…');
copyDir(ELECTRON, APP, (_s, ent) => ent.name !== 'default_app.asar');
const exeOld = path.join(APP, 'electron.exe');
const exeNew = path.join(APP, 'AiFrame.exe');
if (!fs.existsSync(exeOld)) die('Electron 运行时缺少 electron.exe');
fs.renameSync(exeOld, exeNew);

// ---- 3. 控制台应用（resources/app）----
log('装配控制台…');
const appDir = path.join(APP, 'resources/app');
copyFile(path.join(ROOT, 'src/main/main.cjs'), path.join(appDir, 'src/main/main.cjs'));
copyFile(path.join(ROOT, 'src/main/preload.cjs'), path.join(appDir, 'src/main/preload.cjs'));
copyFile(path.join(ROOT, 'src/main/ipc-client.cjs'), path.join(appDir, 'src/main/ipc-client.cjs'));
copyFile(path.join(ROOT, 'src/main/engine-host.cjs'), path.join(appDir, 'src/main/engine-host.cjs'));
copyFile(path.join(ROOT, 'src/main/window-state.cjs'), path.join(appDir, 'src/main/window-state.cjs'));
copyDir(path.join(ROOT, 'dist'), path.join(appDir, 'dist'));
copyFile(path.join(ROOT, 'build/icon.png'), path.join(appDir, 'build/icon.png'));

// 打包后的 package.json：main 指向 src/main/main.cjs，不带 devDependencies
fs.writeFileSync(
  path.join(appDir, 'package.json'),
  JSON.stringify(
    {
      name: 'aimframe-console',
      productName: 'AiFrame',
      version: VER,
      description: 'AiFrame 视觉辅助控制台',
      main: 'src/main/main.cjs',
    },
    null,
    2,
  ) + '\n',
);

// ---- 4. 引擎（resources/engine）----
// engine-host.cjs 的 exePath() 在打包态按 <resources>/engine/helper.exe 解析。
// onnxruntime.dll 必须与 helper.exe 同目录：System32 里的 1.10 会抢先加载。
log('装配引擎…');
const engDir = path.join(APP, 'resources/engine');
copyFile(ENGINE_SRC, path.join(engDir, 'helper.exe'));
if (fs.existsSync(ORT_DLL)) copyFile(ORT_DLL, path.join(engDir, 'onnxruntime.dll'));
else console.warn('[package] 警告：找不到 onnxruntime.dll，推理后端将不可用');
const patterns = path.join(REPO, 'engine/data/patterns.json');
if (fs.existsSync(patterns)) copyFile(patterns, path.join(engDir, 'data/patterns.json'));
else console.warn('[package] 警告：找不到 data/patterns.json，压枪会退化为恒量补偿');

// ---- 5. 体积统计 ----
function du(p) {
  let n = 0;
  const st = fs.statSync(p);
  if (st.isFile()) return st.size;
  for (const e of fs.readdirSync(p)) n += du(path.join(p, e));
  return n;
}
const mb = (n) => (n / 1024 / 1024).toFixed(1) + ' MiB';
log('目录体积：', mb(du(APP)));

// ---- 6. zip ----
const zip = path.join(OUT, `AiFrame-${VER}-win-x64.zip`);
log('压缩…');
try {
  fs.rmSync(zip, { force: true });
  execFileSync(
    'powershell',
    ['-NoProfile', '-NonInteractive', '-Command',
     `Compress-Archive -Path '${APP}' -DestinationPath '${zip}' -CompressionLevel Optimal -Force`],
    { stdio: 'inherit' },
  );
  log('压缩包：', zip, mb(fs.statSync(zip).size));
} catch (e) {
  console.warn('[package] 压缩失败（目录已可用，可直接分发文件夹）：', String(e.message || e));
}

log('完成：');
log('  目录：' + APP);
log('  可执行：' + exeNew);
