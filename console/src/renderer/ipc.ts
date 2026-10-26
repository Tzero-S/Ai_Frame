import type { IpcResponse } from './types';
import { mockInvoke } from './mock';

export interface EngineStatus {
  managed: boolean;   // 由控制台拉起（可停止）
  external: boolean;  // 复用已有实例（不接管生命周期）
  running: boolean;
  pid: number;
  pipe: string;
  exe: string;
  exeExists: boolean;
  uptimeMs: number;
  error: string;
}

export interface ScreenDisplay {
  index: number;
  id: number;
  primary: boolean;
  width: number;       // 物理像素
  height: number;
  cssWidth: number;    // 逻辑像素（CSS）
  cssHeight: number;
  scaleFactor: number;
  bounds: { x: number; y: number; width: number; height: number };
}

export interface ScreenInfo {
  width: number;       // 主屏物理像素
  height: number;
  scaleFactor: number;
  displays: ScreenDisplay[];
}

export interface AimBridge {
  invoke: (
    cmd: string,
    payload?: unknown,
  ) => Promise<{ ok: boolean; response?: unknown; error?: string }>;
  getPipeName: () => Promise<string>;
  screenInfo: () => Promise<ScreenInfo>;
  engine: {
    status: () => Promise<EngineStatus>;
    start: () => Promise<EngineStatus>;
    stop: () => Promise<EngineStatus>;
    restart: () => Promise<EngineStatus>;
  };
}

declare global {
  interface Window {
    aim?: AimBridge;
  }
}

export function isElectron(): boolean {
  return typeof window !== 'undefined' && !!window.aim;
}

// 引擎托管：无 Electron 环境（浏览器预览/mock）时返回离线占位
export async function engineStatus(): Promise<EngineStatus> {
  if (window.aim?.engine) return window.aim.engine.status();
  return {
    managed: false, external: false, running: false, pid: 0, pipe: '',
    exe: '', exeExists: false, uptimeMs: 0, error: '非 Electron 环境',
  };
}

export async function engineStart(): Promise<EngineStatus> {
  if (window.aim?.engine) return window.aim.engine.start();
  return engineStatus();
}

export async function engineStop(): Promise<EngineStatus> {
  if (window.aim?.engine) return window.aim.engine.stop();
  return engineStatus();
}

export async function engineRestart(): Promise<EngineStatus> {
  if (window.aim?.engine) return window.aim.engine.restart();
  return engineStatus();
}

// 屏幕物理分辨率。浏览器预览（无 Electron）时退回 window.screen 的 CSS 像素并标记，
// 调用方应把它当"不可靠值"处理（见 FovSettings 的提示）。
export async function screenInfo(): Promise<ScreenInfo | null> {
  if (window.aim?.screenInfo) return window.aim.screenInfo();
  if (typeof window === 'undefined') return null;
  return {
    width: window.screen.width,
    height: window.screen.height,
    scaleFactor: window.devicePixelRatio || 1,
    displays: [],
  };
}

export async function invoke(cmd: string, payload: unknown = {}): Promise<IpcResponse> {
  if (window.aim) {
    const r = await window.aim.invoke(cmd, payload);
    if (!r.ok) throw new Error(r.error ?? 'ipc error');
    return r.response as IpcResponse;
  }
  return mockInvoke(cmd, payload);
}

export function parseStatus(payload: unknown): unknown {
  if (payload && typeof payload === 'object' && 'status' in payload) {
    const s = (payload as { status?: unknown }).status;
    if (typeof s === 'string') {
      try {
        return JSON.parse(s);
      } catch {
        return null;
      }
    }
    return s;
  }
  return null;
}
