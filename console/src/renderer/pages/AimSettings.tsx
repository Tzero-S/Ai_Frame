import { InputNumber, Segmented, Select, Switch } from 'antd';
import { Field, HotkeyInput, Section, SliderField } from '../components/ui';
import type { ActivationMode, AimConfig, SelectionMode, TargetPreset } from '../types';
import type { PageProps } from './common';

export function AimSettings({ cfg, update }: PageProps) {
  const aim = cfg.aim;

  return (
    <>
      <Section title="激活与热键">
        <Field label="激活方式" desc="Hold 按住 / Toggle 切换 / Switch 轮换">
          <Segmented
            value={aim.activation_mode}
            onChange={(v) => update({ aim: { activation_mode: v as ActivationMode } })}
            options={[
              { label: 'Hold', value: 'hold' },
              { label: 'Toggle', value: 'toggle' },
              { label: 'Switch', value: 'switch' },
            ]}
          />
        </Field>
        <Field label="自瞄主键" desc="按住 / 按下由激活方式决定">
          <HotkeyInput
            value={aim.hotkey_primary}
            onChange={(v) => update({ aim: { hotkey_primary: v } })}
          />
        </Field>
        <Field label="自瞄副键" desc="吸附 / 单次锁定">
          <HotkeyInput
            value={aim.hotkey_secondary}
            onChange={(v) => update({ aim: { hotkey_secondary: v } })}
          />
        </Field>
      </Section>

      <Section title="瞄准范围与选择">
        <Field label="水平半宽" desc="以屏幕中心为原点，左右各延伸">
          <SliderField
            value={aim.scope.half_w}
            min={60}
            max={640}
            suffix=" px"
            onChange={(v) => update({ aim: { scope: { half_w: v } } })}
          />
        </Field>
        <Field label="垂直半高" desc="上下各延伸">
          <SliderField
            value={aim.scope.half_h}
            min={40}
            max={480}
            suffix=" px"
            onChange={(v) => update({ aim: { scope: { half_h: v } } })}
          />
        </Field>
        <Field label="目标选择策略">
          <Select<SelectionMode>
            style={{ width: 180 }}
            value={aim.selection}
            onChange={(v) => update({ aim: { selection: v } })}
            options={[
              { value: 'nearest_crosshair', label: '离准星最近' },
              { value: 'highest_score', label: '最高置信度' },
              { value: 'sticky', label: '锁定保持' },
            ]}
          />
        </Field>
      </Section>

      <Section title="瞄准部位与微调">
        <Field label="部位预设" desc="框内纵向锚点">
          <Select<TargetPreset>
            style={{ width: 180 }}
            value={aim.target_point.preset}
            onChange={(v) => update({ aim: { target_point: { preset: v } } })}
            options={[
              { value: 'head', label: '头部' },
              { value: 'neck', label: '颈部' },
              { value: 'chest', label: '胸部' },
              { value: 'abdomen', label: '腹部' },
              { value: 'legs', label: '腿部' },
              { value: 'custom', label: '自定义' },
            ]}
          />
        </Field>
        <Field label="垂直比例 r" desc="自定义部位时生效（0=框顶, 1=框底）">
          <SliderField
            value={Math.round(aim.target_point.offset_y_ratio * 100)}
            min={0}
            max={100}
            suffix=" %"
            onChange={(v) => update({ aim: { target_point: { offset_y_ratio: v / 100 } } })}
          />
        </Field>
        <Field label="准星偏移 X" desc="准星不在屏幕几何中心时填写（px，右为正）">
          <InputNumber
            min={-2000}
            max={2000}
            addonAfter="px"
            value={aim.crosshair_offset_x}
            onChange={(v) => update({ aim: { crosshair_offset_x: v ?? 0 } })}
          />
        </Field>
        <Field label="准星偏移 Y" desc="准星不在屏幕几何中心时填写（px，下为正）">
          <InputNumber
            min={-2000}
            max={2000}
            addonAfter="px"
            value={aim.crosshair_offset_y}
            onChange={(v) => update({ aim: { crosshair_offset_y: v ?? 0 } })}
          />
        </Field>
        <Field label="微调步长" desc="方向键单次调整量">
          <SliderField
            value={aim.micro_adjust.step_px}
            min={1}
            max={10}
            suffix=" px"
            onChange={(v) => update({ aim: { micro_adjust: { step_px: v } } })}
          />
        </Field>
        <Field label="四向微调" desc="up / down / left / right（px）">
          <span className="row">
            {(['up', 'down', 'left', 'right'] as const).map((k) => (
              <InputNumber
                key={k}
                size="small"
                style={{ width: 70 }}
                value={aim.micro_adjust[k]}
                onChange={(v) =>
                  update({
                    aim: {
                      micro_adjust: { [k]: v ?? 0 } as Partial<AimConfig['micro_adjust']>,
                    },
                  })
                }
              />
            ))}
          </span>
        </Field>
      </Section>

      <Section title="平滑与推进">
        <Field label="启用平滑" desc="关闭则逐帧直出（最跟手、最像机器）">
          <Switch
            checked={aim.smoothing.enabled}
            onChange={(v) => update({ aim: { smoothing: { enabled: v } } })}
          />
        </Field>
        <Field label="平滑模型" desc="critical=临界阻尼(无过冲)；ema=一阶指数">
          <Segmented
            value={aim.smoothing.model}
            onChange={(v) => update({ aim: { smoothing: { model: v as 'ema' | 'critical' } } })}
            options={[
              { value: 'critical', label: '临界阻尼' },
              { value: 'ema', label: 'EMA' },
            ]}
          />
        </Field>
        <Field label="自然频率 ω" desc="临界阻尼模式：越大越跟手（常用 15–40）">
          <SliderField
            value={aim.smoothing.omega}
            min={5}
            max={60}
            step={1}
            onChange={(v) => update({ aim: { smoothing: { omega: v } } })}
          />
        </Field>
        <Field label="jerk 上限" desc="加速度变化率上限（px/s³），0=不限">
          <InputNumber
            min={0}
            max={200000}
            step={500}
            value={aim.smoothing.max_jerk}
            onChange={(v) => update({ aim: { smoothing: { max_jerk: v ?? 0 } } })}
          />
        </Field>
        <Field label="平滑 alpha" desc="EMA 模式：越小越平滑">
          <SliderField
            value={aim.smoothing.alpha}
            min={0.05}
            max={1}
            step={0.05}
            onChange={(v) => update({ aim: { smoothing: { alpha: v } } })}
          />
        </Field>
        <Field label="推进 lead" desc="按延迟对目标做运动外推（0 关闭）">
          <SliderField
            value={aim.smoothing.lead}
            min={0}
            max={1}
            step={0.05}
            onChange={(v) => update({ aim: { smoothing: { lead: v } } })}
          />
        </Field>
        <Field label="延迟补偿" desc="实测延迟（ms）">
          <InputNumber
            min={0}
            max={200}
            addonAfter="ms"
            value={aim.latency_compensation_ms}
            onChange={(v) => update({ aim: { latency_compensation_ms: v ?? 0 } })}
          />
        </Field>
      </Section>

      <Section title="推理性能">
        <Field label="线程数" desc="ORT intra-op 线程；0=自动。小模型（320）设 4–8 可大幅降 CPU">
          <InputNumber
            min={0}
            max={128}
            value={cfg.inference.threads}
            onChange={(v) => update({ inference: { threads: v ?? 0 } })}
          />
        </Field>
        <Field label="输入边长" desc="一般以模型自带尺寸为准，此处仅作兜底">
          <InputNumber
            min={32}
            max={4096}
            value={cfg.inference.imgsz}
            onChange={(v) => update({ inference: { imgsz: v ?? 640 } })}
          />
        </Field>
      </Section>

      <Section title="视场 FOV">
        <Field label="已独立成页" desc="自动查表 / 手动覆盖 / 分辨率提示见左侧「视场 FOV」">
          <span style={{ opacity: 0.7 }}>见「视场 FOV」</span>
        </Field>
      </Section>
    </>
  );
}
