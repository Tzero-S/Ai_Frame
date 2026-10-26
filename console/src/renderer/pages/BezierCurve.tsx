import { Segmented, Switch } from 'antd';
import { CurvePreview, Field, Section, SliderField } from '../components/ui';
import type { BezierType } from '../types';
import type { PageProps } from './common';

export function BezierCurve({ cfg, update }: PageProps) {
  const b = cfg.actuator.kmbox.bezier;

  return (
    <Section
      title="KM 贝塞尔曲线"
      extra={
        <Switch
          checked={b.enabled}
          onChange={(v) => update({ actuator: { kmbox: { bezier: { enabled: v } } } })}
        />
      }
    >
      <Field label="曲线类型" desc="借入 / 偏出 / 缓和">
        <Segmented
          value={b.type}
          onChange={(v) =>
            update({ actuator: { kmbox: { bezier: { type: v as BezierType } } } })
          }
          options={[
            { label: '借入', value: 'ease_in' },
            { label: '偏出', value: 'ease_out' },
            { label: '缓和', value: 'smoothing' },
          ]}
        />
      </Field>
      <Field label="曲线强度" desc="s=0 线性，s=1 完整缓动">
        <SliderField
          value={Math.round(b.strength * 100)}
          min={0}
          max={100}
          suffix=" %"
          onChange={(v) =>
            update({ actuator: { kmbox: { bezier: { strength: v / 100 } } } })
          }
        />
      </Field>
      <div style={{ padding: '12px 0' }}>
        <CurvePreview type={b.type} strength={b.strength} />
      </div>
      <div className="hint">仅 KMBox 通道可用；曲线采样与引擎 BezierTicks 同一公式。</div>
    </Section>
  );
}
