import { useEffect, useState } from 'react';
import { invoke, parseStatus } from './ipc';
import type { StatusSnapshot } from './types';

export function useStatus(intervalMs = 1000): StatusSnapshot | null {
  const [status, setStatus] = useState<StatusSnapshot | null>(null);

  useEffect(() => {
    let alive = true;
    const tick = async () => {
      try {
        const r = await invoke('status.snapshot');
        const s = parseStatus(r.payload) as StatusSnapshot | null;
        if (alive) setStatus(s);
      } catch {
        if (alive) setStatus(null);
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
