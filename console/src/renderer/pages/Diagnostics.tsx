import { useEffect, useState } from 'react';
import { Alert, Button, Tag } from 'antd';
import { Field, Section, SegmentTag } from '../components/ui';
import {
  engineRestart,
  engineStart,
  engineStatus,
  engineStop,
  invoke,
  isElectron,
} from '../ipc';
import type { EngineStatus } from '../ipc';
import { useStatus } from '../useStatus';
import type { PageProps } from './common';

export function Diagnostics(_props: PageProps) {
  const status = useStatus(1000);
  const [hello, setHello] = useState('');
  const [error, setError] = useState('');
  const [configJson, setConfigJson] = useState('');
  const [configVersion, setConfigVersion] = useState(0);
  const [engine, setEngine] = useState<EngineStatus | null>(null);
  const [engineBusy, setEngineBusy] = useState(false);

  const refreshEngine = async () => {
    try {
      setEngine(await engineStatus());
    } catch {
      /* 忽略：状态轮询失败不打断界面 */
    }
  };

  const engineAction = async (fn: () => Promise<EngineStatus>) => {
    setEngineBusy(true);
    try {
      setEngine(await fn());
    } finally {
      setEngineBusy(false);
    }
  };

  useEffect(() => {
    void refreshEngine();
    const t = setInterval(() => void refreshEngine(), 3000);
    return () => clearInterval(t);
  }, []);

  const loadConfig = async () => {
    try {
      const r = await invoke('config.get');
      if (r.code !== 0) throw new Error(r.msg);
      const p = r.payload as { config?: unknown; version?: number } | undefined;
      setConfigVersion(p?.version ?? 0);
      setConfigJson(JSON.stringify(p?.config ?? {}, null, 2));
    } catch (e) {
      setError(e instanceof Error ? e.message : '读取配置失败');
    }
  };

  useEffect(() => {
    let alive = true;
    (async () => {
      try {
        const r = await invoke('engine.hello');
        const p = r.payload as { engine_version?: string } | undefined;
        if (alive) setHello(p?.engine_version ?? 'unknown');
      } catch (e) {
        if (alive) setError(e instanceof Error ? e.message : '连接失败');
      }
    })();
    return () => {
      alive = false;
    };
  }, []);

  return (
    <>
      <Section
        title="引擎进程"
        extra={
          <span className="hint">控制台托管：启动 / 停止 / 重启，不影响外部实例</span>
        }
      >
        <Field label="状态">
          {engine ? (
            <span className="row" style={{ gap: 8 }}>
              <Tag color={engine.running ? 'green' : engine.error ? 'orange' : 'default'}>
                {engine.running ? '运行中' : '未运行'}
              </Tag>
              {engine.external ? <Tag color="blue">外部实例</Tag> : null}
              {engine.managed ? <Tag>由控制台拉起</Tag> : null}
              {!engine.exeExists ? <Tag color="red">可执行文件缺失</Tag> : null}
            </span>
          ) : (
            <span className="mono">—</span>
          )}
        </Field>
        <Field label="PID / 管道">
          <span className="mono">
            {engine?.pid ? engine.pid : '—'} / {engine?.pipe || '—'}
          </span>
        </Field>
        <Field label="可执行文件" desc="可用环境变量 AIM_ENGINE_EXE 覆盖">
          <span className="mono" style={{ wordBreak: 'break-all' }}>
            {engine?.exe || '—'}
          </span>
        </Field>
        <Field label="操作">
          <span className="row" style={{ gap: 8 }}>
            <Button
              size="small"
              type="primary"
              loading={engineBusy}
              disabled={!!engine?.running}
              onClick={() => void engineAction(engineStart)}
            >
              启动
            </Button>
            <Button
              size="small"
              danger
              loading={engineBusy}
              disabled={!engine?.managed}
              onClick={() => void engineAction(engineStop)}
            >
              停止
            </Button>
            <Button
              size="small"
              loading={engineBusy}
              onClick={() => void engineAction(engineRestart)}
            >
              重启
            </Button>
          </span>
        </Field>
        {engine?.error ? <Alert type="warning" showIcon message={engine.error} /> : null}
      </Section>

      <Section title="连接">
        <Field label="模式">
          <Tag color={isElectron() ? 'blue' : 'orange'}>
            {isElectron() ? 'Electron 命名管道' : '浏览器 Mock'}
          </Tag>
        </Field>
        <Field label="引擎版本">
          <span className="mono">{hello || '—'}</span>
        </Field>
        {error ? <Alert type="warning" showIcon message={error} /> : null}
      </Section>

      <Section
        title="配置"
        extra={
          <Button size="small" onClick={() => void loadConfig()}>
            读取配置
          </Button>
        }
      >
        <Field label="applied_version">
          <span className="mono">{configVersion}</span>
        </Field>
        <pre
          className="mono"
          style={{ maxHeight: 220, overflow: 'auto', margin: '8px 0 0' }}
        >
          {configJson || '—'}
        </pre>
      </Section>

      <Section title="运行状态">
        <Field label="运行中">
          <Tag color={status?.running ? 'green' : 'red'}>{status?.running ? '是' : '否'}</Tag>
        </Field>
        <Field label="运行时长">
          <span className="mono">
            {status ? (status.uptime_ms / 1000).toFixed(0) : 0} s
          </span>
        </Field>
        <Field label="FPS">
          <span className="mono">{status ? status.fps.toFixed(1) : '—'}</span>
        </Field>
        <Field label="帧数 / 丢弃">
          <span className="mono">{status ? `${status.frames} / ${status.drop_count}` : '—'}</span>
        </Field>
        <Field label="当前目标">
          <Tag color={status?.has_target ? 'green' : 'default'}>
            {status?.has_target ? '有' : '无'}
          </Tag>
        </Field>
      </Section>

      <Section title="分段状态">
        <Field label="采集">
          <SegmentTag state={status?.segments.capture ?? 'failed'} />
        </Field>
        <Field label="推理">
          <SegmentTag state={status?.segments.inference ?? 'failed'} />
        </Field>
        <Field label="执行">
          <SegmentTag state={status?.segments.actuator ?? 'failed'} />
        </Field>
      </Section>

      <Section
        title="实时检测"
        extra={<Tag>{status?.detections?.length ?? 0} 个</Tag>}
      >
        {(status?.detections ?? []).map((d, i) => (
          <div className="metric" key={`${d.class_id}-${i}`}>
            <span>
              #{i} {d.class_id === 1 ? 'Head' : 'Body'}
            </span>
            <b>{d.score.toFixed(2)}</b>
          </div>
        ))}
        {!(status?.detections && status.detections.length > 0) ? (
          <div className="hint">无（引擎运行 --preview 时推送）</div>
        ) : null}
      </Section>

      <Section title="时序（ms）">
        <div className="tiles">
          <div className="tile">
            <div className="tile-l">采集</div>
            <div className="tile-v">{(status?.timings.cap_ms ?? 0).toFixed(2)}</div>
          </div>
          <div className="tile">
            <div className="tile-l">推理</div>
            <div className="tile-v">{(status?.timings.infer_ms ?? 0).toFixed(2)}</div>
          </div>
          <div className="tile">
            <div className="tile-l">后处理</div>
            <div className="tile-v">{(status?.timings.post_ms ?? 0).toFixed(2)}</div>
          </div>
          <div className="tile">
            <div className="tile-l">下发</div>
            <div className="tile-v">{(status?.timings.send_ms ?? 0).toFixed(2)}</div>
          </div>
        </div>
      </Section>
    </>
  );
}
