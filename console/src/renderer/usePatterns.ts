// 弹道库条目（§5.7.1）：从引擎侧数据文件读取，供「弹道库」下拉使用。
//
// 为什么不写死在界面里：patterns.json 是引擎真正加载的那份数据，
// 界面写死武器名会出现「下拉里有、引擎里没有」（选 AWP 结果引擎退化为恒量补偿）
// 或者「引擎里有、下拉里没有」（录了新弹道在界面上选不到）。
// 这里以引擎为真源；引擎不在线（浏览器预览 / mock）时退化为内置清单。
import { useEffect, useState } from 'react';
import { invoke } from './ipc';

export interface PatternItem {
  id: string;
  name: string;
  rpm: number;
  samples: number;
}

// 离线兜底：仅用于没有引擎时的界面预览，不代表引擎真的加载成功。
const FALLBACK: PatternItem[] = [
  { id: 'm4a1s', name: 'M4A1-S', rpm: 600, samples: 0 },
  { id: 'ak47', name: 'AK-47', rpm: 600, samples: 0 },
];

export interface PatternsState {
  items: PatternItem[];
  /** 引擎实际打开的文件（绝对路径）；空串表示没读到 */
  resolvedPath: string;
  /** 非空 = 读取失败原因（引擎离线 / 文件缺失 / 解析失败） */
  error: string;
  loading: boolean;
  /** 是否来自引擎真源（false = 内置兜底清单） */
  fromEngine: boolean;
}

export function usePatterns(path: string): PatternsState {
  const [state, setState] = useState<PatternsState>({
    items: FALLBACK,
    resolvedPath: '',
    error: '',
    loading: true,
    fromEngine: false,
  });

  useEffect(() => {
    let alive = true;
    setState((s) => ({ ...s, loading: true }));

    // 路径是输入框，边打字边发请求会打爆 IPC：等 500ms 静默再查
    const timer = setTimeout(() => {
      void (async () => {
        try {
          const r = await invoke('weapon.patterns', { path });
          if (!alive) return;
          if (r.code !== 0) {
            setState({
              items: FALLBACK,
              resolvedPath: '',
              error: r.msg || '读取弹道库失败',
              loading: false,
              fromEngine: false,
            });
            return;
          }
          const p = r.payload as
            | { items?: PatternItem[]; path?: string }
            | undefined;
          const items = Array.isArray(p?.items) ? p.items : [];
          setState({
            items: items.length > 0 ? items : FALLBACK,
            resolvedPath: p?.path ?? '',
            error: items.length > 0 ? '' : '弹道库为空',
            loading: false,
            fromEngine: items.length > 0,
          });
        } catch (e) {
          if (!alive) return;
          setState({
            items: FALLBACK,
            resolvedPath: '',
            error: e instanceof Error ? e.message : '引擎未连接',
            loading: false,
            fromEngine: false,
          });
        }
      })();
    }, 500);

    return () => {
      alive = false;
      clearTimeout(timer);
    };
  }, [path]);

  return state;
}
