import { InputNumber, Segmented, Switch, Tag } from 'antd';
import { Field, Section, SliderField } from '../components/ui';
import type { AntiDetectLevel } from '../types';
import type { PageProps } from './common';

export function AntiDetectSettings({ cfg, update }: PageProps) {
  const ad = cfg.anti_detect;
  const off = ad.level === 'off';

  return (
    <>
      <Section title="总体等级">
        <Field label="防检测等级" desc="off 关闭全部行为层措施；standard/strict 由各子项控制">
          <Segmented
            value={ad.level}
            onChange={(v) => update({ anti_detect: { level: v as AntiDetectLevel } })}
            options={[
              { label: 'Off', value: 'off' },
              { label: 'Standard', value: 'standard' },
              { label: 'Strict', value: 'strict' },
            ]}
          />
        </Field>
        <Field label="当前状态">
          <span className="row">
            <Tag color={off ? 'default' : 'green'}>{off ? '已关闭' : '已启用'}</Tag>
            <Tag>行为层 / 进程层 / 网络层</Tag>
          </span>
        </Field>
      </Section>

      <Section title="L1 行为层（§5.12.1）">
        <Field label="移动抖动 σ" desc="高斯抖动幅度（px）；0 表示关闭位置抖动">
          <SliderField
            value={ad.jitter_px}
            min={0}
            max={3}
            step={0.1}
            suffix=" px"
            onChange={(v) => update({ anti_detect: { jitter_px: v } })}
          />
        </Field>
        <Field label="时机抖动比例" desc="步长/间隔的 ±比例；0 表示关闭">
          <SliderField
            value={Math.round(ad.timing_jitter_ratio * 100)}
            min={0}
            max={50}
            suffix=" %"
            onChange={(v) => update({ anti_detect: { timing_jitter_ratio: v / 100 } })}
          />
        </Field>
        <Field label="随机化时机" desc="关闭后时长恒定（更机械，但便于复现）">
          <Switch
            checked={ad.randomize_timing}
            onChange={(v) => update({ anti_detect: { randomize_timing: v } })}
          />
        </Field>
        <Field label="目标切换迟滞" desc="新目标需连续 N 帧才切换；0 表示不迟滞">
          <InputNumber
            min={0}
            max={10}
            value={ad.switch_hysteresis_frames}
            onChange={(v) => update({ anti_detect: { switch_hysteresis_frames: v ?? 0 } })}
          />
        </Field>
        <Field label="无目标降频" desc="无目标时把循环降到该帧率，降低持续占用">
          <InputNumber
            min={5}
            max={120}
            addonAfter="fps"
            value={ad.idle_fps}
            onChange={(v) => update({ anti_detect: { idle_fps: v ?? 30 } })}
          />
        </Field>
      </Section>

      <Section title="L2 进程层（§5.12.2）">
        <Field label="失焦即停" desc="前台非游戏窗口时停止推理与下发，并隐藏覆盖层">
          <Switch
            checked={ad.hide_on_focus_loss}
            onChange={(v) => update({ anti_detect: { hide_on_focus_loss: v } })}
          />
        </Field>
        <Field label="工具窗口样式" desc="WS_EX_TOOLWINDOW：不出现在任务栏与 Alt-Tab">
          <Switch
            checked={ad.hide_window}
            onChange={(v) => update({ anti_detect: { hide_window: v } })}
          />
        </Field>
        <Field label="线程名中性化" desc="工作线程命名为 w0/w1，避免可识别前缀">
          <Switch
            checked={ad.neutral_thread_names}
            onChange={(v) => update({ anti_detect: { neutral_thread_names: v } })}
          />
        </Field>
        <Field label="张量用后清零" desc="推理输出缓冲解码后立即清零">
          <Switch
            checked={ad.zero_tensors}
            onChange={(v) => update({ anti_detect: { zero_tensors: v } })}
          />
        </Field>
        <Field label="日志路径剥离" desc="日志/错误信息中去掉绝对路径">
          <Switch
            checked={ad.strip_paths}
            onChange={(v) => update({ anti_detect: { strip_paths: v } })}
          />
        </Field>
        <Field label="排除可疑模块" desc="加载前过滤已知敏感模块">
          <Switch
            checked={ad.exclude_suspicious_modules}
            onChange={(v) => update({ anti_detect: { exclude_suspicious_modules: v } })}
          />
        </Field>
        <Field
          label="反调试"
          desc="默认关闭：开启本身会形成可观测的对抗特征，仅调试期使用"
        >
          <span className="row">
            <Switch
              checked={ad.anti_debug}
              onChange={(v) => update({ anti_detect: { anti_debug: v } })}
            />
            {ad.anti_debug ? <Tag color="orange">已开启（不建议生产使用）</Tag> : null}
          </span>
        </Field>
      </Section>

      <Section title="L3 网络层（§5.12.4）">
        <Field label="授权通道" desc="M10 接入：TLS 1.3 + 证书固定 + 请求签名">
          <Tag>由 M10 授权客户端实现</Tag>
        </Field>
      </Section>
    </>
  );
}
