import { Button, Input, InputNumber, Select, message } from 'antd';
import { Field, Section } from '../components/ui';
import { invoke } from '../ipc';
import type { ActuatorConfig } from '../types';
import type { PageProps } from './common';

export function KmboxSettings({ cfg, update }: PageProps) {
  const a = cfg.actuator;

  const test = async () => {
    try {
      const r = await invoke('actuator.test', {});
      if (r.code === 0) message.success('通道自检通过');
      else message.warning(r.msg);
    } catch (e) {
      message.warning(e instanceof Error ? e.message : '自检失败');
    }
  };

  return (
    <>
      <Section
        title="执行通道"
        extra={
          <Button size="small" onClick={() => void test()}>
            通道自检
          </Button>
        }
      >
        <Field label="通道">
          <Select<ActuatorConfig['channel']>
            style={{ width: 180 }}
            value={a.channel}
            onChange={(v) => update({ actuator: { channel: v } })}
            options={[
              { value: 'sendinput', label: 'SendInput' },
              { value: 'ghub', label: 'GHUB' },
              { value: 'kmbox', label: 'KMBox' },
              { value: 'serial', label: '串口' },
            ]}
          />
        </Field>
      </Section>

      <Section title="KMBox 设置">
        <Field label="KmnetIP">
          <Input
            style={{ width: 180 }}
            value={a.kmbox.ip}
            onChange={(e) => update({ actuator: { kmbox: { ip: e.target.value } } })}
          />
        </Field>
        <Field label="KmnetPort">
          <InputNumber
            min={1}
            max={65535}
            value={a.kmbox.port}
            onChange={(v) => update({ actuator: { kmbox: { port: v ?? 8088 } } })}
          />
        </Field>
        <Field label="KmnetUID" desc="8 位十六进制">
          <Input
            style={{ width: 140 }}
            maxLength={8}
            value={a.kmbox.uid}
            onChange={(e) =>
              update({ actuator: { kmbox: { uid: e.target.value.toUpperCase() } } })
            }
          />
        </Field>
      </Section>

      <Section title="串口（ESP32 下位机）">
        <Field label="端口">
          <Input
            style={{ width: 120 }}
            value={a.serial.port}
            onChange={(e) => update({ actuator: { serial: { port: e.target.value } } })}
          />
        </Field>
        <Field label="波特率">
          <InputNumber
            min={9600}
            max={921600}
            step={9600}
            value={a.serial.baud}
            onChange={(v) => update({ actuator: { serial: { baud: v ?? 115200 } } })}
          />
        </Field>
      </Section>
    </>
  );
}
