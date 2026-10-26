import { useEffect, useRef, useState } from 'react';
import { invoke, parseStatus } from './ipc';
import type { StatusSnapshot } from './types';

export interface TelemetrySample {
  fps: number;
  infer: number;
}

export interface Telemetry {
  status: StatusSnapshot | null;
  /** 最近 capacity 次采样的滚动历史（旧→新） */
  history: TelemetrySample[];
}

// 1Hz 轮询 status.snapshot，并保留滚动历史供右栏曲线使用。
// 取不到数据（引擎未启动 / mock 模式）时清空历史，避免曲线停在旧值误导。
export function useTelemetry(intervalMs = 1000, capacity = 60): Telemetry {
  const [status, setStatus] = useState<StatusSnapshot | null>(null);
  const [history, setHistory] = useState<TelemetrySample[]>([]);
  const buf = useRef<TelemetrySample[]>([]);

  useEffect(() => {
    let alive = true;
    const tick = async () => {
      try {
        const r = await invoke('status.snapshot');
        const s = parseStatus(r.payload) as StatusSnapshot | null;
        if (!alive) return;
        setStatus(s);
        if (s) {
          buf.current = [...buf.current, { fps: s.fps, infer: s.timings.infer_ms }].slice(
            -capacity,
          );
          setHistory(buf.current);
        } else {
          buf.current = [];
          setHistory([]);
        }
      } catch {
        if (!alive) return;
        setStatus(null);
        buf.current = [];
        setHistory([]);
      }
    };
    void tick();
    const timer = window.setInterval(() => void tick(), intervalMs);
    return () => {
      alive = false;
      window.clearInterval(timer);
    };
  }, [intervalMs, capacity]);

  return { status, history };
}
