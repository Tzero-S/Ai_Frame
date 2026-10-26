import { useEffect, useState } from 'react';
import type { ReactNode } from 'react';
import { Button, Slider, Tag } from 'antd';
import type { BezierType, SegmentState } from '../types';

export function Section({
  title,
  extra,
  children,
}: {
  title: string;
  extra?: ReactNode;
  children: ReactNode;
}) {
  return (
    <section className="card">
      <header className="card-h">
        <h3>{title}</h3>
        <div>{extra}</div>
      </header>
      <div className="card-b">{children}</div>
    </section>
  );
}

export function Field({
  label,
  desc,
  children,
}: {
  label: string;
  desc?: string;
  children: ReactNode;
}) {
  return (
    <div className="field">
      <div className="field-l">
        <div className="field-t">{label}</div>
        {desc ? <div className="field-d">{desc}</div> : null}
      </div>
      <div className="field-r">{children}</div>
    </div>
  );
}

export function SliderField({
  value,
  min,
  max,
  step = 1,
  suffix,
  onChange,
}: {
  value: number;
  min: number;
  max: number;
  step?: number;
  suffix?: string;
  onChange: (v: number) => void;
}) {
  const text = Number.isInteger(value) ? String(value) : value.toFixed(2);
  return (
    <div className="slider-field">
      <Slider min={min} max={max} step={step} value={value} onChange={onChange} tooltip={{ open: false }} />
      <span className="slider-val">
        {text}
        {suffix ?? ''}
      </span>
    </div>
  );
}

const MOUSE = ['MOUSE1', 'MOUSE2', 'MOUSE3', 'MOUSE4', 'MOUSE5'];

function keyName(e: KeyboardEvent): string {
  const c = e.code;
  if (c.startsWith('Key')) return c.slice(3);
  if (c.startsWith('Digit')) return c.slice(5);
  if (/^F\d{1,2}$/.test(c)) return c;
  if (c.startsWith('Numpad')) return c.toUpperCase();
  const map: Record<string, string> = {
    ArrowUp: 'UP',
    ArrowDown: 'DOWN',
    ArrowLeft: 'LEFT',
    ArrowRight: 'RIGHT',
    Escape: 'ESC',
    Space: 'SPACE',
    ShiftLeft: 'LSHIFT',
    ShiftRight: 'RSHIFT',
    ControlLeft: 'LCTRL',
    ControlRight: 'RCTRL',
    AltLeft: 'LALT',
    AltRight: 'RALT',
    Tab: 'TAB',
    Enter: 'ENTER',
    Backspace: 'BACKSPACE',
  };
  return map[c] ?? c.toUpperCase();
}

export function HotkeyInput({
  value,
  onChange,
}: {
  value: string;
  onChange: (v: string) => void;
}) {
  const [capturing, setCapturing] = useState(false);

  useEffect(() => {
    if (!capturing) return;
    const onKey = (e: KeyboardEvent) => {
      e.preventDefault();
      e.stopPropagation();
      onChange(keyName(e));
      setCapturing(false);
    };
    const onMouse = (e: MouseEvent) => {
      e.preventDefault();
      e.stopPropagation();
      onChange(MOUSE[e.button] ?? `MOUSE${e.button + 1}`);
      setCapturing(false);
    };
    window.addEventListener('keydown', onKey, true);
    window.addEventListener('mousedown', onMouse, true);
    return () => {
      window.removeEventListener('keydown', onKey, true);
      window.removeEventListener('mousedown', onMouse, true);
    };
  }, [capturing, onChange]);

  return (
    <span className="hotkey">
      <Tag className="keycap">{value || '未设置'}</Tag>
      <Button size="small" onClick={() => setCapturing(true)}>
        {capturing ? '按键中…' : '绑定'}
      </Button>
      <Button size="small" type="text" onClick={() => onChange('')}>
        清除
      </Button>
    </span>
  );
}

function fractions(type: BezierType, strength: number): [number, number] {
  const t =
    type === 'ease_in' ? [0.25, 0.75] : type === 'ease_out' ? [0.75, 0.25] : [0.42, 0.58];
  const s = Math.max(0, Math.min(1, strength));
  return [1 / 3 + s * (t[0] - 1 / 3), 2 / 3 + s * (t[1] - 2 / 3)];
}

export function CurvePreview({ type, strength }: { type: BezierType; strength: number }) {
  const [f1, f2] = fractions(type, strength);
  const W = 300;
  const H = 130;
  const P = 14;
  const x = (v: number) => P + v * (W - 2 * P);
  const y = (v: number) => H - P - v * (H - 2 * P);
  const d = `M ${x(0)} ${y(0)} C ${x(f1)} ${y(f1)} ${x(f2)} ${y(f2)} ${x(1)} ${y(1)}`;

  return (
    <svg className="curve" viewBox={`0 0 ${W} ${H}`} role="img" aria-label="bezier preview">
      <defs>
        <linearGradient id="curveGrad" x1="0" y1="0" x2="1" y2="0">
          <stop offset="0%" stopColor="#1677FF" />
          <stop offset="100%" stopColor="#5B4BE0" />
        </linearGradient>
      </defs>
      <rect x="0.5" y="0.5" width={W - 1} height={H - 1} rx="12" fill="#FFFFFF" stroke="#E5E8EC" />
      <line x1={x(0)} y1={y(0)} x2={x(1)} y2={y(1)} stroke="#E5E8EC" strokeDasharray="4 4" />
      <path d={d} fill="none" stroke="url(#curveGrad)" strokeWidth="2.5" strokeLinecap="round" />
      <circle cx={x(f1)} cy={y(f1)} r="3" fill="#1677FF" />
      <circle cx={x(f2)} cy={y(f2)} r="3" fill="#5B4BE0" />
    </svg>
  );
}

const SEGMENT: Record<SegmentState, { color: string; text: string }> = {
  running: { color: 'green', text: '运行' },
  degraded: { color: 'orange', text: '降级' },
  failed: { color: 'red', text: '失败' },
};

export function SegmentTag({ state }: { state: SegmentState }) {
  const m = SEGMENT[state] ?? SEGMENT.failed;
  return <Tag color={m.color}>{m.text}</Tag>;
}
