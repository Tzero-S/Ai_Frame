import type { AppConfig } from './types';

// 与引擎 §13.2 默认参数保持一致
export const defaultConfig: AppConfig = {
  capture: {
    mode: 'dxgi',
    roi: { mode: 'center_square', edge: 640 },
    output_monitor: 0,
    game_window: { title_pattern: '', auto_detect: true },
    scrcpy: { serial: '', host: '127.0.0.1', port: 27183, edge: 640 },
  },
  aim: {
    hotkey_primary: 'MOUSE2',
    hotkey_secondary: 'MOUSE4',
    activation_mode: 'hold',
    scope: { half_w: 320, half_h: 240 },
    target_point: { preset: 'head', offset_y_ratio: 0.12 },
    crosshair_offset_x: 0,
    crosshair_offset_y: 0,
    micro_adjust: { up: 0, down: 0, left: 0, right: 0, step_px: 2 },
    selection: 'nearest_crosshair',
    smoothing: {
      enabled: true,
      model: 'critical',
      alpha: 0.35,
      omega: 25,
      max_jerk: 0,
      beta: 0,
      lead: 0,
    },
    latency_compensation_ms: 0,
  },
  weapon: {
    recoil: {
      enabled: true,
      strength: 5,
      hotkey: '',
      rpm: 600,
      pattern_id: 'm4a1s',
      patterns_path: 'data/patterns.json',
      horizontal_gain: 0,
      horizontal_source: 'manual',
      base_px: 200,
      strength_ads: 0,
    },
    trigger: {
      enabled: false,
      type: 'rifle',
      key: '',
      scope_follow_aim: true,
      aim_offset_px: 0,
      rifle_delay_ms: 70,
      sniper_delay_ms: 0,
    },
  },
  actuator: {
    channel: 'sendinput',
    kmbox: {
      ip: '192.168.2.188',
      port: 8088,
      uid: '529C5C53',
      bezier: { enabled: false, type: 'ease_in', strength: 0.5 },
    },
    serial: { port: 'COM1', baud: 115200 },
  },
  fov: {
    auto: true,
    hfov_deg: 103,
    vfov_deg: 59,
    manual_override: false,
    ads_levels: [],
    ads_zoom: 1,
  },
  hotkeys: {
    toggle_hud: 'F9',
    toggle_team: 'F5',
    toggle_aim: 'F4',
    adhesion_minus: 'F11',
    adhesion_plus: 'F12',
    push_switch: 'NUMPAD8',
    target_up: 'UP',
    target_down: 'DOWN',
    target_left: 'LEFT',
    target_right: 'RIGHT',
  },
  inference: {
    backend: 'dml',
    target_fps: 120,
    imgsz: 640,
    conf_threshold: 0.35,
    iou_threshold: 0.45,
    threads: 0,
    async_capture: true,
    selected_class_names: [],
    class_names: [],
  },
  anti_detect: {
    level: 'standard',
    hide_window: true,
    hide_on_focus_loss: true,
    randomize_timing: true,
    strip_paths: true,
    exclude_suspicious_modules: true,
    jitter_px: 0.8,
    timing_jitter_ratio: 0.1,
    switch_hysteresis_frames: 2,
    idle_fps: 30,
    anti_debug: false,
    zero_tensors: true,
    neutral_thread_names: true,
  },
};

export type DeepPartial<T> = {
  [K in keyof T]?: T[K] extends object ? DeepPartial<T[K]> : T[K];
};

export function mergeDeep<T>(base: T, patch: DeepPartial<T>): T {
  if (typeof base !== 'object' || base === null) {
    return (patch as unknown as T) ?? base;
  }
  const out: Record<string, unknown> = Array.isArray(base)
    ? ([...(base as unknown[])] as unknown as Record<string, unknown>)
    : { ...(base as Record<string, unknown>) };

  const p = patch as Record<string, unknown>;
  for (const key of Object.keys(p)) {
    const pv = p[key];
    const bv = (base as Record<string, unknown>)[key];
    // 数组整体替换：逐下标合并会让"变短的数组"保留旧尾巴
    // （例：["body","head"] 收到 ["head"] 会变成 ["head","head"]）
    if (Array.isArray(pv)) {
      out[key] = [...pv];
      continue;
    }
    out[key] =
      typeof bv === 'object' && bv !== null && typeof pv === 'object' && pv !== null
        ? mergeDeep(bv as unknown, pv as DeepPartial<unknown>)
        : pv;
  }
  return out as T;
}
