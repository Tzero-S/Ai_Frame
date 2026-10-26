import { Alert, Input, InputNumber, Segmented, Select, Switch, Button, Slider} from 'antd';
import { Field, HotkeyInput, Section, SliderField } from '../components/ui';
import type { TriggerType } from '../types';
import type { PageProps } from './common';
import { usePatterns } from '../usePatterns';

export function RecoilTrigger({ cfg, update }: PageProps) {
  const { recoil, trigger } = cfg.weapon;
  // 弹道库以引擎侧数据文件为真源（见 usePatterns 注释）
  const pat = usePatterns(recoil.patterns_path);
  const known = pat.items.some((p) => p.id === recoil.pattern_id);
  const missing = pat.fromEngine && !known;

  return (
    <>
      <Section
        title="压枪"
        extra={
          <Switch
            checked={recoil.enabled}
            onChange={(v) => update({ weapon: { recoil: { enabled: v } } })}
          />
        }
      >
        <Field
          label="压枪强度"
          desc="0–30。15 = 峰值等于「峰值补偿像素」；30 = 2 倍。高倍镜要更大，自己调到压得住为止"
        >
          <SliderField
            value={recoil.strength}
            min={0}
            max={30}
            step={0.5}
            onChange={(v) => update({ weapon: { recoil: { strength: v } } })}
          />
        </Field>
        <Field
          label="开镜强度"
          desc="0 = 跟随上面的强度（不区分）。想区分就填一个值，开镜时自动切过去；不做任何倍率换算"
        >
          <InputNumber
            min={0}
            max={30}
            step={0.5}
            value={recoil.strength_ads}
            onChange={(v) => update({ weapon: { recoil: { strength_ads: v ?? 0 } } })}
          />
        </Field>
        <Field label="武器射速 RPM">
          <InputNumber
            min={30}
            max={2000}
            step={10}
            value={recoil.rpm}
            onChange={(v) => update({ weapon: { recoil: { rpm: v ?? 600 } } })}
          />
        </Field>
        <Field
          label="弹道库"
          desc={
            pat.loading
              ? '正在读取弹道库…'
              : pat.error
                ? `读取失败：${pat.error}（下拉为内置清单，引擎可能仍用配置文件里的值）`
                : `共 ${pat.items.length} 条 · ${pat.resolvedPath}`
          }
        >
          <Select
            style={{ width: 220 }}
            value={recoil.pattern_id}
            onChange={(v) => update({ weapon: { recoil: { pattern_id: v } } })}
            options={pat.items.map((p) => ({
              value: p.id,
              label: p.rpm > 0 ? `${p.name} · ${p.rpm} RPM` : p.name,
            }))}
          />
        </Field>
        {missing ? (
          <Alert
            type="warning"
            showIcon
            style={{ margin: '0 0 12px' }}
            message={`弹道库内没有「${recoil.pattern_id}」`}
            description={
              <span>
                引擎会退化为恒量垂直补偿（不用曲线形状）。可改选库里已有的条目：
                {pat.items.slice(0, 4).map((p) => (
                  <Button
                    key={p.id}
                    size="small"
                    type="link"
                    onClick={() => update({ weapon: { recoil: { pattern_id: p.id } } })}
                  >
                    {p.name}
                  </Button>
                ))}
              </span>
            }
          />
        ) : null}
        <Field label="弹道路径" desc="patterns.json 位置；相对路径先按工作目录、再按 exe 目录查找">
          <Input
            style={{ width: 420 }}
            value={recoil.patterns_path}
            onChange={(e) => update({ weapon: { recoil: { patterns_path: e.target.value } } })}
            placeholder="data/patterns.json"
          />
        </Field>
        <Field label="压枪热键" desc="按下切换压枪开关">
          <HotkeyInput
            value={recoil.hotkey}
            onChange={(v) => update({ weapon: { recoil: { hotkey: v } } })}
          />
        </Field>
        <Field
          label="左右偏移"
          desc="横向补偿 = 垂直补偿 × 该系数。0 = 只压垂直；负值向左、正值向右。实测后坐力为单向斜飞，调一个值即可"
        >
          <div style={{ display: 'flex', alignItems: 'center', gap: 12, width: 460 }}>
            <Button
              size="small"
              onClick={() => update({ weapon: { recoil: { horizontal_gain: -0.1 } } })}
              title="快速设为向左 0.1"
            >
              左
            </Button>
            <Slider
              style={{ flex: 1 }}
              min={-1.5}
              max={1.5}
              step={0.05}
              value={recoil.horizontal_gain}
              onChange={(v) => update({ weapon: { recoil: { horizontal_gain: v } } })}
              tooltip={{ formatter: (v) => `${(v ?? 0).toFixed(2)}` }}
            />
            <span className="mono" style={{ width: 48, textAlign: 'right' }}>
              {recoil.horizontal_gain.toFixed(2)}
            </span>
            <Button
              size="small"
              onClick={() => update({ weapon: { recoil: { horizontal_gain: 0.1 } } })}
              title="快速设为向右 0.1"
            >
              右
            </Button>
          </div>
        </Field>
        <Field label="峰值补偿像素" desc="曲线峰值(shape=1.0)对应的下压像素，强度 15 时生效。用于标定压枪总量">
          <InputNumber
            min={0}
            max={6000}
            step={10}
            addonAfter="px"
            value={recoil.base_px}
            onChange={(v) => update({ weapon: { recoil: { base_px: v ?? 200 } } })}
          />
        </Field>
      </Section>

      <Section
        title="扳机"
        extra={
          <Switch
            checked={trigger.enabled}
            onChange={(v) => update({ weapon: { trigger: { enabled: v } } })}
          />
        }
      >
        {!trigger.key ? (
          <Alert
            type="error"
            showIcon
            message="未设置扳机键：扳机整体不生效"
            style={{ margin: '8px 0 4px' }}
          />
        ) : null}
        <Field label="扳机类型">
          <Segmented
            value={trigger.type}
            onChange={(v) => update({ weapon: { trigger: { type: v as TriggerType } } })}
            options={[
              { label: '步枪(连发)', value: 'rifle' },
              { label: '狙击(点射)', value: 'sniper' },
            ]}
          />
        </Field>
        <Field label="扳机键" desc="未设置时扳机整体不生效">
          <HotkeyInput
            value={trigger.key}
            onChange={(v) => update({ weapon: { trigger: { key: v } } })}
          />
        </Field>
        <Field label="范围跟随自瞄">
          <Switch
            checked={trigger.scope_follow_aim}
            onChange={(v) => update({ weapon: { trigger: { scope_follow_aim: v } } })}
          />
        </Field>
        <Field label="瞄准部位偏移" desc="命中判定线在框内的纵向位置">
          <SliderField
            value={trigger.aim_offset_px}
            min={-100}
            max={100}
            suffix=" px"
            onChange={(v) => update({ weapon: { trigger: { aim_offset_px: v } } })}
          />
        </Field>
        <Field label="步枪扳机延时">
          <InputNumber
            min={0}
            max={500}
            addonAfter="ms"
            value={trigger.rifle_delay_ms}
            onChange={(v) => update({ weapon: { trigger: { rifle_delay_ms: v ?? 70 } } })}
          />
        </Field>
      </Section>
    </>
  );
}
