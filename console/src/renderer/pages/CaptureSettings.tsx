import { Input, InputNumber, Segmented, Switch, Tag } from 'antd';
import { Field, Section, SliderField } from '../components/ui';
import type { CaptureMode, RoiMode } from '../types';
import type { PageProps } from './common';

export function CaptureSettings({ cfg, update }: PageProps) {
  const cap = cfg.capture;
  const isScrcpy = cap.mode === 'scrcpy';

  return (
    <>
      <Section title="采集通道">
        <Field label="采集模式" desc="DXGI 零拷贝 / GDI 兼容 / scrcpy 投屏">
          <Segmented
            value={cap.mode}
            onChange={(v) => update({ capture: { mode: v as CaptureMode } })}
            options={[
              { label: 'DXGI', value: 'dxgi' },
              { label: 'GDI', value: 'gdi' },
              { label: 'scrcpy', value: 'scrcpy' },
            ]}
          />
        </Field>
        <Field label="显示器序号" desc="多显示器时从 0 开始枚举">
          <InputNumber
            min={0}
            max={8}
            value={cap.output_monitor}
            onChange={(v) => update({ capture: { output_monitor: v ?? 0 } })}
          />
        </Field>
        <Field label="游戏窗口标题" desc="用于失焦即停判定；留空则不判定焦点">
          <Input
            style={{ width: 240 }}
            placeholder="例如 VALORANT"
            value={cap.game_window.title_pattern}
            onChange={(e) =>
              update({ capture: { game_window: { title_pattern: e.target.value } } })
            }
          />
        </Field>
        <Field label="自动检测游戏窗口">
          <Switch
            checked={cap.game_window.auto_detect}
            onChange={(v) => update({ capture: { game_window: { auto_detect: v } } })}
          />
        </Field>
      </Section>

      <Section title="采集区域（ROI）">
        <Field label="ROI 模式" desc="center_square 只截屏幕中心；full 截整屏">
          <Segmented
            value={cap.roi.mode}
            onChange={(v) => update({ capture: { roi: { mode: v as RoiMode } } })}
            options={[
              { label: '中心方形', value: 'center_square' },
              { label: '整屏', value: 'full' },
            ]}
          />
        </Field>
        <Field label="ROI 边长" desc="模型输入小于该值时会被下采样；大于该值时引擎自动抬到模型尺寸">
          <SliderField
            value={cap.roi.edge}
            min={320}
            max={1280}
            suffix=" px"
            onChange={(v) => update({ capture: { roi: { edge: v } } })}
          />
        </Field>
      </Section>

      <Section title="投屏参数">
        {!isScrcpy ? (
          <Field label="提示" desc="仅 scrcpy 模式使用以下参数">
            <Tag>当前未启用</Tag>
          </Field>
        ) : null}
        <Field label="设备序列号" desc="adb devices 中的 serial">
          <Input
            style={{ width: 240 }}
            placeholder="例如 127.0.0.1:5555"
            value={cap.scrcpy.serial}
            onChange={(e) => update({ capture: { scrcpy: { serial: e.target.value } } })}
          />
        </Field>
        <Field label="主机 / 端口">
          <span className="row">
            <Input
              style={{ width: 150 }}
              value={cap.scrcpy.host}
              onChange={(e) => update({ capture: { scrcpy: { host: e.target.value } } })}
            />
            <InputNumber
              min={1}
              max={65535}
              value={cap.scrcpy.port}
              onChange={(v) => update({ capture: { scrcpy: { port: v ?? 27183 } } })}
            />
          </span>
        </Field>
        <Field label="投屏截边边长" desc="§5.2.3：投屏画面的采样边长">
          <SliderField
            value={cap.scrcpy.edge}
            min={320}
            max={960}
            suffix=" px"
            onChange={(v) => update({ capture: { scrcpy: { edge: v } } })}
          />
        </Field>
      </Section>
    </>
  );
}
