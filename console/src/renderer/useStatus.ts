import { useEffect, useState } from 'react';
import { invoke, parseStatus } from './ipc';
import type { StatusSnapshot } from './types';

export function useStatus(intervalMs = 1000): StatusSnapshot | null {
  const [status, setStatus] = useState<StatusSnapshot | null>(null);

  useEffect(() => {
    let alive = true;
    // 在途保护：IPC 变慢时 setInterval 会不断叠加请求，晚到的旧快照还会覆盖新快照
    // （界面上的帧率/耗时来回跳）。同一时刻只允许一个请求在飞。
    let inFlight = false;
    const tick = async () => {
      if (inFlight) return;
      inFlight = true;
      try {
        const r = await invoke('status.snapshot');
        const s = parseStatus(r.payload) as StatusSnapshot | null;
        if (alive) setStatus(s);
      } catch {
        if (alive) setStatus(null);
      } finally {
        inFlight = false;
      }
    };
    void tick();
    const timer = window.setInterval(() => {
      void tick();
    }, intervalMs);
    return () => {
      alive = false;
      window.clearInterval(timer);
    };
  }, [intervalMs]);

  return status;
}
