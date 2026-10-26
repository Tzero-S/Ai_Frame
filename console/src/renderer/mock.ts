import type { IpcResponse, StatusSnapshot } from './types';

const mockStatus: StatusSnapshot = {
  running: true,
  uptime_ms: 123456,
  fps: 118.4,
  timings: { cap_ms: 0.6, infer_ms: 6.2, post_ms: 1.1, send_ms: 0.4 },
  frames: 98765,
  drop_count: 12,
  segments: { capture: 'running', inference: 'running', actuator: 'degraded' },
  has_target: true,
  seq: 42,
  detections: [
    { class_id: 0, score: 0.8374, x: 450, y: 436, w: 38, h: 91 },
    { class_id: 1, score: 0.6406, x: 459, y: 437, w: 11, h: 14 },
  ],
};

export function mockInvoke(cmd: string, _payload: unknown): IpcResponse {
  switch (cmd) {
    case 'engine.hello':
      return {
        id: 'mock',
        code: 0,
        msg: 'ok',
        payload: { engine_version: '1.0.0-mock', schema_version: 4, mode: 'mock' },
      };
    case 'status.snapshot':
      return {
        id: 'mock',
        code: 0,
        msg: 'ok',
        payload: { status: JSON.stringify(mockStatus) },
      };
    case 'config.get':
      return { id: 'mock', code: 0, msg: 'ok', payload: { config: {}, version: 0 } };
    case 'config.apply':
      return { id: 'mock', code: 0, msg: 'ok', payload: { applied_version: 1 } };
    case 'model.import':
      return { id: 'mock', code: 400, msg: '未连接引擎（mock）' };
    case 'model.preview':
      return {
        id: 'mock',
        code: 0,
        msg: 'ok',
        payload: { detections: [], raw_count: 0, infer_ms: 0 },
      };
    case 'fov.estimate':
      return {
        id: 'mock',
        code: 0,
        msg: 'ok',
        payload: {
          screen: { width: 1920, height: 1080, monitor: 0, detected: true },
          aspect: 1920 / 1080,
          auto_hfov_deg: 103,
          auto_vfov_deg: 70.53,
          hfov_deg: 103,
          vfov_deg: 70.53,
          manual: false,
          inconsistent: false,
          ads_applied: false,
        },
      };
    case 'weapon.patterns':
      // mock 模式下给一份样例，让弹道库下拉在浏览器预览里也能用
      return {
        id: 'mock',
        code: 0,
        msg: 'ok',
        payload: {
          path: '(mock) data/patterns.json',
          items: [
            { id: 'm4a1s', name: 'M4A1-S', rpm: 600, samples: 31 },
            { id: 'ak47', name: 'AK-47', rpm: 600, samples: 31 },
            { id: 'vector', name: 'Vector', rpm: 1200, samples: 25 },
            { id: 'm416', name: 'M416', rpm: 700, samples: 31 },
          ],
        },
      };
    default:
      return { id: 'mock', code: 404, msg: 'unknown cmd' };
  }
}
