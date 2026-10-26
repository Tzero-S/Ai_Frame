export type ActivationMode = 'hold' | 'toggle' | 'switch';
export type SelectionMode = 'nearest_crosshair' | 'highest_score' | 'sticky';
export type TargetPreset = 'head' | 'neck' | 'chest' | 'abdomen' | 'legs' | 'custom';
export type TriggerType = 'rifle' | 'sniper';
export type BezierType = 'ease_in' | 'ease_out' | 'smoothing';
export type SegmentState = 'running' | 'degraded' | 'failed';

export interface AimConfig {
  hotkey_primary: string;
  hotkey_secondary: string;
  activation_mode: ActivationMode;
  scope: { half_w: number; half_h: number };
  target_point: { preset: TargetPreset; offset_y_ratio: number };
  /** 准星相对屏幕中心的偏移（px），准星不在几何中心的游戏需要配 */
  crosshair_offset_x: number;
  crosshair_offset_y: number;
  micro_adjust: { up: number; down: number; left: number; right: number; step_px: number };
  selection: SelectionMode;
  smoothing: {
    enabled: boolean;
    model: 'ema' | 'critical';
    alpha: number;
    omega: number;
    max_jerk: number;
    beta: number;
    lead: number;
  };
  latency_compensation_ms: number;
}

export interface WeaponConfig {
  recoil: { enabled: boolean; strength: number; hotkey: string; rpm: number; pattern_id: string;
  /** 弹道库 JSON 路径；相对路径先按工作目录、再按 exe 目录查找 */
  patterns_path: string;
    /** 横向补偿 = 垂直 × 该系数；0=只压垂直，负=向左，正=向右 */
    horizontal_gain: number;
    /** manual = 用 horizontal_gain；pattern = 用曲线自带 dx */
    horizontal_source: 'manual' | 'pattern';
    /** 曲线峰值(shape=1.0)对应的下压像素（strength=15 时生效） */
    base_px: number;
    /** 开镜强度；0 = 跟随 strength。仅 fov.ads_zoom>1 时生效，纯手动无换算 */
    strength_ads: number };
  trigger: {
    enabled: boolean;
    type: TriggerType;
    key: string;
    scope_follow_aim: boolean;
    aim_offset_px: number;
    rifle_delay_ms: number;
    sniper_delay_ms: number;
  };
}

export interface ActuatorConfig {
  channel: 'sendinput' | 'ghub' | 'kmbox' | 'serial';
  kmbox: { ip: string; port: number; uid: string; bezier: { enabled: boolean; type: BezierType; strength: number } };
  serial: { port: string; baud: number };
}

export interface FovConfig {
  // 键名与引擎 JSON 一致（§5.1.2：fov.auto），不是 C++ 成员名 auto_fov
  auto: boolean;
  hfov_deg: number;
  vfov_deg: number;
  manual_override: boolean;
  /** 开镜倍率表：zoom → 水平 FOV；空表示不区分开镜 */
  ads_levels: { zoom: number; hfov_deg: number }[];
  ads_zoom: number;
}

export type CaptureMode = 'dxgi' | 'gdi' | 'scrcpy';
export type RoiMode = 'center_square' | 'full';

export interface CaptureConfig {
  mode: CaptureMode;
  roi: { mode: RoiMode; edge: number };
  output_monitor: number;
  game_window: { title_pattern: string; auto_detect: boolean };
  scrcpy: { serial: string; host: string; port: number; edge: number };
}

export type AntiDetectLevel = 'off' | 'standard' | 'strict';

export interface AntiDetectConfig {
  level: AntiDetectLevel;
  hide_window: boolean;
  hide_on_focus_loss: boolean;
  randomize_timing: boolean;
  strip_paths: boolean;
  exclude_suspicious_modules: boolean;
  jitter_px: number;
  timing_jitter_ratio: number;
  switch_hysteresis_frames: number;
  idle_fps: number;
  anti_debug: boolean;
  zero_tensors: boolean;
  neutral_thread_names: boolean;
}

export interface HotkeysConfig {
  toggle_hud: string;
  toggle_team: string;
  toggle_aim: string;
  adhesion_minus: string;
  adhesion_plus: string;
  push_switch: string;
  target_up: string;
  target_down: string;
  target_left: string;
  target_right: string;
}

export interface InferenceConfig {
  backend: 'dml' | 'cpu' | 'tensorrt';
  target_fps: number;
  imgsz: number;
  conf_threshold: number;
  iou_threshold: number;
  /** ORT intra-op 线程数：0 = 自动；小模型建议 4~8 以降低 CPU 占用 */
  threads: number;
  /** 采集与推理并行（双缓冲）；关闭回退同步 grab */
  async_capture: boolean;
  /** v4：按"名字"选择瞄准类别（空 = 加载模型时自动挑选 body/head 类） */
  selected_class_names: string[];
  /** 引擎回灌的模型真实类别（只读展示，UI 不主动改） */
  class_names: string[];
}

export interface AppConfig {
  capture: CaptureConfig;
  aim: AimConfig;
  weapon: WeaponConfig;
  actuator: ActuatorConfig;
  fov: FovConfig;
  hotkeys: HotkeysConfig;
  inference: InferenceConfig;
  anti_detect: AntiDetectConfig;
}

export interface StatusSnapshot {
  running: boolean;
  uptime_ms: number;
  fps: number;
  timings: { cap_ms: number; infer_ms: number; post_ms: number; send_ms: number };
  frames: number;
  drop_count: number;
  segments: { capture: SegmentState; inference: SegmentState; actuator: SegmentState };
  has_target: boolean;
  seq: number;
  detections?: Detection[];
}

export interface ModelTensorInfo {
  name: string;
  dtype: string;
  shape: number[];
  dynamic: boolean;
}

export interface ModelInfo {
  ok: boolean;
  error?: string;
  id: string;
  alias: string;
  file: string;
  sha256: string;
  input: ModelTensorInfo;
  output: ModelTensorInfo;
  classes: string[];
  num_classes: number;
  num_kpts: number;
  created_at: string;
}

export interface Detection {
  class_id: number;
  score: number;
  x: number;
  y: number;
  w: number;
  h: number;
}

export interface IpcResponse {
  id: string;
  code: number;
  msg: string;
  payload?: unknown;
}
