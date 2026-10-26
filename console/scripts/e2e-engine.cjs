'use strict';
// 控制台 IPC 客户端 ↔ 真实引擎命名管道联调（自包含：自己拉起引擎，跑完自己收）。
//
// 引擎已改为"每次启动随机管道名 + %TEMP%\aimframe.pipe 发现文件"（去标识化，§7），
// 所以这里不再假设固定管道名：统一走 EngineHost 的发现/拉起逻辑。
const fs = require('fs');
const { AimPipeClient } = require('../src/main/ipc-client.cjs');
const { EngineHost, DISCOVERY } = require('../src/main/engine-host.cjs');

const MODEL =
  process.env.AIM_VALORANT_MODEL ||
  'D:/MyProject/Test.Project/test/projects/VALORANT_aimbot/model/best_s960_fp16_dml.onnx';

(async () => {
  // 允许外部指定管道（连已在跑的引擎）；否则由 EngineHost 负责拉起/复用
  let pipe = process.env.AIM_PIPE || '';
  const host = new EngineHost();
  if (!pipe) {
    const st = await host.start();
    if (!st.running || !st.pipe) throw new Error('引擎未就绪：' + (st.error || '未知原因'));
    pipe = st.pipe;
    console.log('引擎:', st.managed ? '控制台拉起' : '复用已有实例', '| pid', st.pid, '| pipe', st.pipe);
  }
  console.log('发现文件:', (() => { try { return fs.readFileSync(DISCOVERY, 'utf8').trim(); } catch { return '(无)'; } })());

  const c = new AimPipeClient(pipe, 8000);
  await c.connect();

  const hello = await c.invoke('engine.hello');
  console.log('engine.hello ->', JSON.stringify(hello));
  if (hello.code !== 0) throw new Error('hello failed');

  const st = await c.invoke('status.snapshot');
  console.log('status.snapshot -> code', st.code, 'status type', typeof st.payload?.status);
  if (st.code !== 0) throw new Error('status failed');

  const cg = await c.invoke('config.get');
  console.log('config.get -> code', cg.code, 'version', cg.payload?.version,
              'schema', cg.payload?.config?.schema_version);
  if (cg.code !== 0) throw new Error('config.get failed');

  const applied = await c.invoke('config.apply', {
    config: { schema_version: 4, aim: { scope: { half_w: 400 } } },
  });
  console.log('config.apply -> code', applied.code, 'applied_version', applied.payload?.applied_version);
  if (applied.code !== 0) throw new Error('config.apply failed: ' + applied.msg);

  const cg2 = await c.invoke('config.get');
  const halfW = cg2.payload?.config?.aim?.scope?.half_w;
  console.log('config.get after apply -> half_w', halfW);
  if (halfW !== 400) throw new Error('config apply did not persist');

  // 弹道库列表（产品外壳的「弹道库」下拉依赖它）
  const pat = await c.invoke('weapon.patterns');
  const items = pat.payload?.items ?? [];
  console.log('weapon.patterns -> code', pat.code, '| 条目', items.length, '| 路径', pat.payload?.path);
  if (pat.code !== 0) throw new Error('weapon.patterns failed: ' + pat.msg);
  if (items.length === 0) throw new Error('weapon.patterns 返回空列表');
  if (!items.every((i) => i.id && i.name)) throw new Error('weapon.patterns 条目缺 id/name');

  // 分辨率识别 + FOV 查表（界面只显示引擎算的值，所以这条必须通）
  const fov = await c.invoke('fov.estimate', {});
  console.log('fov.estimate -> code', fov.code,
              '| 分辨率', `${fov.payload?.screen?.width}x${fov.payload?.screen?.height}`,
              '| HFOV', fov.payload?.auto_hfov_deg?.toFixed(2),
              '| VFOV', fov.payload?.auto_vfov_deg?.toFixed(2));
  if (fov.code !== 0) throw new Error('fov.estimate failed: ' + fov.msg);
  if (!fov.payload?.screen?.detected) throw new Error('fov.estimate 未识别到分辨率');
  const ar = fov.payload.screen.width / fov.payload.screen.height;
  if (Math.abs(ar - fov.payload.aspect) > 1e-6) throw new Error('aspect 与分辨率不一致');
  // 16:10 必须落在 97° 附近（回归：曾错成 110°）
  const fov1610 = await c.invoke('fov.estimate', { width: 2560, height: 1600 });
  const hfov1610 = fov1610.payload?.auto_hfov_deg ?? 0;
  console.log('fov.estimate(2560x1600) -> HFOV', hfov1610.toFixed(2), 'VFOV',
              fov1610.payload?.auto_vfov_deg?.toFixed(2));
  if (Math.abs(hfov1610 - 97) > 0.3) throw new Error('16:10 HFOV 应为 97°，实际 ' + hfov1610);

  if (fs.existsSync(MODEL)) {
    const mi = await c.invoke('model.import', { path: MODEL });
    console.log('model.import -> code', mi.code);
    if (mi.code !== 0) throw new Error('model.import failed: ' + mi.msg);
    const model = mi.payload?.model;
    console.log('  alias:', model?.alias, 'classes:', JSON.stringify(model?.classes));
  } else {
    console.log('model.import -> 跳过（模型不存在：' + MODEL + '）');
  }

  c.close();
  if (host.status().managed) await host.stop();
  console.log('E2E OK');
  process.exit(0);
})().catch((e) => {
  console.error('E2E FAIL:', e.message);
  process.exit(1);
});
