import { Tag } from 'antd';
import { Field, HotkeyInput, Section } from '../components/ui';
import type { HotkeysConfig } from '../types';
import type { PageProps } from './common';

const LABELS: Record<keyof HotkeysConfig, string> = {
  toggle_hud: '弹出隐藏',
  toggle_team: '切换阵营',
  toggle_aim: '自瞄切换',
  adhesion_minus: '吸附强度 −',
  adhesion_plus: '吸附强度 +',
  push_switch: '推进开关',
  target_up: '部位向上',
  target_down: '部位向下',
  target_left: '部位向左',
  target_right: '部位向右',
};

export function HotkeyBinding({ cfg, update }: PageProps) {
  const keys = Object.keys(LABELS) as (keyof HotkeysConfig)[];
  const counts = new Map<string, number>();
  for (const k of keys) {
    const v = cfg.hotkeys[k];
    if (v) counts.set(v, (counts.get(v) ?? 0) + 1);
  }

  return (
    <>
      <Section title="功能热键" extra={<span className="hint">同一键不可绑定到两个互斥项</span>}>
        {keys.map((k) => {
          const v = cfg.hotkeys[k];
          const dup = !!v && (counts.get(v) ?? 0) > 1;
          return (
            <Field key={k} label={LABELS[k]} desc={dup ? '与其他项冲突' : undefined}>
              <span className="row">
                {dup ? <Tag color="red">冲突</Tag> : null}
                <HotkeyInput
                  value={v}
                  onChange={(nv) =>
                    update({ hotkeys: { [k]: nv } as Partial<HotkeysConfig> })
                  }
                />
              </span>
            </Field>
          );
        })}
      </Section>

      <Section title="自瞄 / 武器热键">
        <Field label="自瞄主键">
          <HotkeyInput
            value={cfg.aim.hotkey_primary}
            onChange={(v) => update({ aim: { hotkey_primary: v } })}
          />
        </Field>
        <Field label="自瞄副键">
          <HotkeyInput
            value={cfg.aim.hotkey_secondary}
            onChange={(v) => update({ aim: { hotkey_secondary: v } })}
          />
        </Field>
        <Field label="压枪热键" desc="未设置时仅受总开关控制">
          <HotkeyInput
            value={cfg.weapon.recoil.hotkey}
            onChange={(v) => update({ weapon: { recoil: { hotkey: v } } })}
          />
        </Field>
        <Field label="扳机键" desc="未设置时扳机整体不生效">
          <HotkeyInput
            value={cfg.weapon.trigger.key}
            onChange={(v) => update({ weapon: { trigger: { key: v } } })}
          />
        </Field>
      </Section>
    </>
  );
}
