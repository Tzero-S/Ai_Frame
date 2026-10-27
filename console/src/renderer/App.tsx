import { useEffect, useMemo, useState } from 'react';
import { Button, ConfigProvider, Tag, message } from 'antd';
import { lightTheme } from './theme';
import { defaultConfig, mergeDeep } from './defaults';
import type { DeepPartial } from './defaults';
import type { AppConfig } from './types';
import { invoke, isElectron } from './ipc';
import { useStatus } from './useStatus';
import { SegmentTag } from './components/ui';
import { AimSettings } from './pages/AimSettings';
import { FovSettings } from './pages/FovSettings';
import { CaptureSettings } from './pages/CaptureSettings';
import { AntiDetectSettings } from './pages/AntiDetectSettings';
import { HotkeyBinding } from './pages/HotkeyBinding';
import { RecoilTrigger } from './pages/RecoilTrigger';
import { KmboxSettings } from './pages/KmboxSettings';
import { BezierCurve } from './pages/BezierCurve';
import { Models } from './pages/Models';
import { Diagnostics } from './pages/Diagnostics';
import logo from './assets/logo.png';
import './styles.css';

type PageKey =
  | 'aim'
  | 'fov'
  | 'capture'
  | 'hotkey'
  | 'weapon'
  | 'actuator'
  | 'bezier'
  | 'model'
  | 'antidetect'
  | 'diag';

interface PageMeta {
  key: PageKey;
  label: string;
  title: string;
  crumb: string;
}

const PAGES: PageMeta[] = [
  { key: 'aim', label: '准星设置', title: '准星与瞄准', crumb: 'Aim' },
  { key: 'fov', label: '视场 FOV', title: '视场与分辨率', crumb: 'FOV' },
  { key: 'capture', label: '采集设置', title: '采集与投屏', crumb: 'Capture' },
  { key: 'hotkey', label: '快捷键绑定', title: '快捷键绑定', crumb: 'Hotkeys' },
  { key: 'weapon', label: '压枪 / 扳机', title: '压枪与扳机', crumb: 'Weapon' },
  { key: 'actuator', label: 'KMBox 设置', title: 'KMBox 与执行通道', crumb: 'Actuator' },
  { key: 'bezier', label: 'KM 贝塞尔曲线', title: 'KM 贝塞尔曲线', crumb: 'Bezier' },
  { key: 'model', label: '模型管理', title: '模型管理与预览', crumb: 'Models' },
  { key: 'antidetect', label: '防检测', title: '防检测与 OPSEC', crumb: 'OPSEC' },
  { key: 'diag', label: '状态 / 诊断', title: '状态与诊断', crumb: 'Diagnostics' },
];

function renderPage(
  page: PageKey,
  cfg: AppConfig,
  update: (patch: DeepPartial<AppConfig>) => void,
) {
  switch (page) {
    case 'aim':
      return <AimSettings cfg={cfg} update={update} />;
    case 'fov':
      return <FovSettings cfg={cfg} update={update} />;
    case 'capture':
      return <CaptureSettings cfg={cfg} update={update} />;
    case 'antidetect':
      return <AntiDetectSettings cfg={cfg} update={update} />;
    case 'hotkey':
      return <HotkeyBinding cfg={cfg} update={update} />;
    case 'weapon':
      return <RecoilTrigger cfg={cfg} update={update} />;
    case 'actuator':
      return <KmboxSettings cfg={cfg} update={update} />;
    case 'bezier':
      return <BezierCurve cfg={cfg} update={update} />;
    case 'model':
      return <Models cfg={cfg} update={update} />;
    case 'diag':
      return <Diagnostics cfg={cfg} update={update} />;
    default:
      return null;
  }
}

// 稳定序列化：对象键按字典序递归输出，数组保持原序。
// 用途是"内容相同即视为相同"，与 JSON.stringify 的插入顺序敏感行为区分开。
function stableStringify(v: unknown): string {
  const norm = (x: unknown): unknown => {
    if (Array.isArray(x)) return x.map(norm);
    if (x && typeof x === 'object') {
      const o = x as Record<string, unknown>;
      const out: Record<string, unknown> = {};
      for (const k of Object.keys(o).sort()) out[k] = norm(o[k]);
      return out;
    }
    return x;
  };
  return JSON.stringify(norm(v));
}

export default function App() {
  const [page, setPage] = useState<PageKey>('aim');
  const [cfg, setCfg] = useState<AppConfig>(defaultConfig);
  // 已下发给引擎的配置快照（用于"未应用变更"提示）
  const [applied, setApplied] = useState<string>(() => stableStringify(defaultConfig));
  const status = useStatus(1000);
  // 连接状态以"能否取到 status.snapshot"为准，而不是"是否跑在 Electron 里"
  const inElectron = isElectron();
  const online = inElectron && status !== null;
  const connClass = !inElectron ? ' mock' : online ? '' : ' off';
  const connText = !inElectron ? 'Mock 模式' : online ? 'Engine 已连接' : 'Engine 未连接';

  // 启动时以引擎侧配置为真源初始化（§5.1.3 单一真源）。
  // 只渲染部分分组也不会丢字段：未在 UI 出现的节（capture / profile / anti_detect）
  // 会随 cfg 原样回传，配合引擎的 merge 语义保持不被重置。
  useEffect(() => {
    let alive = true;
    void (async () => {
      try {
        const r = await invoke('config.get');
        if (!alive || r.code !== 0) return;
        const remote = (r.payload as { config?: unknown } | undefined)?.config;
        if (remote && typeof remote === 'object') {
          setCfg((prev) => {
            const merged = mergeDeep(prev, remote as unknown as DeepPartial<AppConfig>);
            setApplied(stableStringify(merged));
            return merged;
          });
        }
      } catch {
        // 引擎未启动 / mock 模式：保持本地默认值
      }
    })();
    return () => {
      alive = false;
    };
  }, []);

  const update = (patch: DeepPartial<AppConfig>) =>
    setCfg((prev) => mergeDeep(prev, patch));

  // 稳定序列化：JSON.stringify 的键序依赖插入顺序，字段被重建后即使内容相同
  // 也会被判成"有改动"（顶栏一直亮着"未下发"）。这里按键名递归排序后再比较。
  const dirty = useMemo(() => stableStringify(cfg) !== applied, [cfg, applied]);

  const applyConfig = async () => {
    try {
      const r = await invoke('config.apply', { config: { schema_version: 4, ...cfg } });
      if (r.code !== 0) throw new Error(r.msg);
      const p = r.payload as { applied_version?: number } | undefined;
      setApplied(stableStringify(cfg));
      message.success(`配置已应用 v${p?.applied_version ?? '?'}`);
    } catch (e) {
      message.error(e instanceof Error ? e.message : '应用失败');
    }
  };

  const meta = useMemo(() => PAGES.find((p) => p.key === page) ?? PAGES[0], [page]);

  return (
    <ConfigProvider theme={lightTheme}>
      <div className="ambient" />
      <div className="app">
        <aside className="rail">
          <div className="rail-card">
            <div className="brand">
              <img className="brand-mark" src={logo} alt="AiFrame" draggable={false} />
              <div>
                <div className="brand-name">AiFrame</div>
                <div className="brand-sub">视觉辅助控制台</div>
              </div>
            </div>
            <nav className="nav">
              {PAGES.map((p) => (
                <button
                  key={p.key}
                  className={`nav-item${p.key === page ? ' active' : ''}`}
                  onClick={() => setPage(p.key)}
                >
                  <span>{p.label}</span>
                  {p.key === page ? <span className="nav-dot" /> : null}
                </button>
              ))}
            </nav>
          </div>
          <div className="rail-foot">
            <div className="conn">
              <span className={`conn-dot${connClass}`} />
              {connText}
            </div>
            <div className="hint">schema v4 · 白色主题</div>
          </div>
        </aside>

        <main className="main">
          <header className="topbar">
            <div className="topbar-l">
              <div className="crumb">{meta.crumb}</div>
              <h1>{meta.title}</h1>
            </div>
            <div className="topbar-r">
              {dirty ? (
                <span className="dirty-hint" title="有修改尚未下发到引擎">
                  <span className="dirty-dot" />
                  未应用
                </span>
              ) : null}
              <Button
                size="small"
                type={dirty ? 'primary' : 'default'}
                onClick={() => void applyConfig()}
              >
                应用配置
              </Button>
              <span className="clock">{new Date().toLocaleTimeString()}</span>
            </div>
          </header>
          <div className="page" key={page}>
            {renderPage(page, cfg, update)}
          </div>
        </main>

        <aside className="right">
          <div className="right-card">
            <div className="right-title">运行状态</div>
            <div className="metric">
              <span>FPS</span>
              <b>{status ? status.fps.toFixed(1) : '—'}</b>
            </div>
            <div className="metric">
              <span>推理</span>
              <b>{status ? `${status.timings.infer_ms.toFixed(1)} ms` : '—'}</b>
            </div>
            <div className="metric">
              <span>目标</span>
              <b>{status?.has_target ? '已锁定' : '无'}</b>
            </div>
          </div>

          <div className="right-card">
            <div className="right-title">分段</div>
            <div className="metric">
              <span>采集</span>
              <SegmentTag state={status?.segments.capture ?? 'failed'} />
            </div>
            <div className="metric">
              <span>推理</span>
              <SegmentTag state={status?.segments.inference ?? 'failed'} />
            </div>
            <div className="metric">
              <span>执行</span>
              <SegmentTag state={status?.segments.actuator ?? 'failed'} />
            </div>
          </div>

          <div className="right-card">
            <div className="right-title">快速状态</div>
            <div className="metric">
              <span>压枪</span>
              <Tag color={cfg.weapon.recoil.enabled ? 'green' : 'default'}>
                {cfg.weapon.recoil.enabled ? '开' : '关'}
              </Tag>
            </div>
            <div className="metric">
              <span>扳机</span>
              <Tag color={cfg.weapon.trigger.enabled ? 'orange' : 'default'}>
                {cfg.weapon.trigger.enabled ? '开' : '关'}
              </Tag>
            </div>
            <div className="metric">
              <span>通道</span>
              <b>{cfg.actuator.channel}</b>
            </div>
          </div>
        </aside>
      </div>
    </ConfigProvider>
  );
}
