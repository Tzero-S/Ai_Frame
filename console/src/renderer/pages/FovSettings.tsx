import { useCallback, useEffect, useState } from 'react';
import { Alert, Button, InputNumber, Switch, Tag } from 'antd';
import { Field, Section } from '../components/ui';
import type { PageProps } from './common';
import { invoke, isElectron, screenInfo } from '../ipc';
import type { ScreenInfo } from '../ipc';

// 分辨率 → FOV 的查表**只在引擎里有一份**（fov/fov_math.h）。
// 这里原本抄了一份同样的锚点，两边一旦不同步就会出现"界面显示 103°、引擎用 110°"，
// 而 16:10 锚点写错（110°）正是靠这种重复藏了很久：两处一起错，测试也只测一处。
// 现在界面只显示引擎算出来的值（fov.estimate）。
interface FovEstimate {
  screen: { width: number; height: number; monitor: number; detected: boolean };
  aspect: number;
  auto_hfov_deg: number;
  auto_vfov_deg: number;
  hfov_deg: number;
  vfov_deg: number;
  manual: boolean;
  inconsistent: boolean;
  ads_applied: boolean;
}

export function FovSettings({ cfg, update }: PageProps) {
  const fov = cfg.fov;
  const [est, setEst] = useState<FovEstimate | null>(null);
  const [err, setErr] = useState('');
  const [busy, setBusy] = useState(false);
  const [screen, setScreen] = useState<ScreenInfo | null>(null);

  // 引擎侧识别（真源）。传空 payload = 用引擎自己识别的分辨率。
  const refresh = useCallback(async () => {
    setBusy(true);
    try {
      const r = await invoke('fov.estimate', {});
      if (r.code === 0) {
        setEst(r.payload as FovEstimate);
        setErr('');
      } else {
        const p = r.payload as FovEstimate | undefined;
        setEst(p ?? null);
        setErr(r.msg || '分辨率识别失败');
      }
    } catch (e) {
      setEst(null);
      setErr(e instanceof Error ? e.message : '引擎未连接');
    } finally {
      setBusy(false);
    }
  }, []);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  // 控制台自己看到的屏幕（物理像素，主进程算 size*scaleFactor）
  useEffect(() => {
    let alive = true;
    void screenInfo().then((s) => {
      if (alive) setScreen(s);
    });
    return () => {
      alive = false;
    };
  }, []);

  const engineW = est?.screen.width ?? 0;
  const engineH = est?.screen.height ?? 0;
  const aspect = est?.aspect ?? 0;
  const autoH = est?.auto_hfov_deg ?? 0;
  const autoV = est?.auto_vfov_deg ?? 0;
  const drift = est?.inconsistent ?? false;

  // 引擎识别的采集屏 vs 控制台所在的主屏：多显示器下这俩常常不是一块
  const consoleDiffers =
    !!est && !!screen && (screen.width !== engineW || screen.height !== engineH);

  return (
    <>
      <Section
        title="视场来源"
        extra={
          <Button size="small" loading={busy} onClick={() => void refresh()}>
            重新识别
          </Button>
        }
      >
        <Field label="自动识别分辨率" desc="按显示分辨率查表得出 HFOV，再按公式推出 VFOV">
          <Switch
            checked={fov.auto}
            onChange={(v) => update({ fov: { auto: v } })}
          />
        </Field>
        <Field
          label="引擎识别分辨率"
          desc={
            est
              ? `采集目标：显示器 #${est.screen.monitor}（capture.output_monitor）· 物理像素`
              : '引擎未连接时无法识别，下面显示的是控制台自身屏幕信息'
          }
        >
          <span className="row">
            {est && est.screen.detected ? (
              <>
                <Tag color="blue">
                  {engineW}×{engineH}
                </Tag>
                <span style={{ opacity: 0.65 }}>宽高比 {aspect.toFixed(3)}</span>
              </>
            ) : (
              <Tag color="orange">{err || '识别失败'}</Tag>
            )}
          </span>
        </Field>
        {consoleDiffers ? (
          <Alert
            type="warning"
            showIcon
            style={{ margin: '0 0 12px' }}
            message="控制台所在屏与引擎采集屏不同"
            description={
              <span>
                控制台（主屏）{screen?.width}×{screen?.height}
                {screen ? ` @${Math.round(screen.scaleFactor * 100)}%` : ''}；引擎采集显示器 #
                {est?.screen.monitor} {engineW}×{engineH}。FOV 以引擎采集的那块屏为准。
              </span>
            }
          />
        ) : null}
        <Field label="自动值" desc="16:9 → 103°；VFOV 由公式 vfov=2·atan(tan(hfov/2)/aspect)">
          <span className="row">
            {est ? (
              <>
                <Tag>HFOV {autoH.toFixed(1)}°</Tag>
                <Tag>VFOV {autoV.toFixed(1)}°</Tag>
              </>
            ) : (
              <span style={{ opacity: 0.6 }}>—（需引擎在线）</span>
            )}
          </span>
        </Field>
        {screen && isElectron() ? (
          <Field
            label="控制台屏幕"
            desc="主进程给出的物理像素（window.screen 是 CSS 像素，125% 缩放下会小一圈）"
          >
            <span className="row">
              <Tag>
                {screen.width}×{screen.height}
              </Tag>
              <span style={{ opacity: 0.65 }}>
                缩放 {Math.round(screen.scaleFactor * 100)}% · 共 {screen.displays.length} 块屏
              </span>
            </span>
          </Field>
        ) : null}
      </Section>

      <Section title="开镜倍率表（ADS）">
        <Field
          label="当前倍率"
          desc="1 = 腰射。引擎按倍率在表中插值，开镜后 FOV 变小、像素↔角度换算随之修正"
        >
          <InputNumber
            min={1}
            max={64}
            step={0.5}
            value={fov.ads_zoom}
            onChange={(v) => update({ fov: { ads_zoom: v ?? 1 } })}
          />
        </Field>
        {fov.ads_levels.map((lv, i) => (
          <Field key={i} label={`档位 ${i + 1}`} desc={`${lv.zoom}x → ${lv.hfov_deg}°`}>
            <div className="row" style={{ gap: 8 }}>
              <InputNumber
                min={1}
                max={64}
                step={0.5}
                addonBefore="倍率"
                value={lv.zoom}
                onChange={(v) => {
                  const next = fov.ads_levels.map((x, j) =>
                    j === i ? { ...x, zoom: v ?? 1 } : x,
                  );
                  update({ fov: { ads_levels: next } });
                }}
              />
              <InputNumber
                min={1}
                max={179}
                addonAfter="°"
                value={lv.hfov_deg}
                onChange={(v) => {
                  const next = fov.ads_levels.map((x, j) =>
                    j === i ? { ...x, hfov_deg: v ?? 103 } : x,
                  );
                  update({ fov: { ads_levels: next } });
                }}
              />
              <Button
                size="small"
                danger
                onClick={() =>
                  update({ fov: { ads_levels: fov.ads_levels.filter((_, j) => j !== i) } })
                }
              >
                删除
              </Button>
            </div>
          </Field>
        ))}
        <Field label="新增档位" desc="腰射档建议先加 1x，再按实际倍镜添加">
          <Button
            size="small"
            onClick={() =>
              update({ fov: { ads_levels: [...fov.ads_levels, { zoom: 2, hfov_deg: 51 }] } })
            }
          >
            添加一档
          </Button>
        </Field>
      </Section>

      <Section title="手动覆盖">
        <Field label="HFOV 水平视场角">
          <InputNumber
            min={40}
            max={140}
            step={0.5}
            addonAfter="°"
            value={fov.hfov_deg}
            onChange={(v) => update({ fov: { hfov_deg: v ?? 103 } })}
          />
        </Field>
        <Field label="VFOV 垂直视场角" desc="引擎自动模式下不使用该值（由公式推出）">
          <InputNumber
            min={20}
            max={120}
            step={0.5}
            addonAfter="°"
            value={fov.vfov_deg}
            onChange={(v) => update({ fov: { vfov_deg: v ?? 59 } })}
          />
        </Field>
        <Field
          label="启用手动覆盖"
          desc={
            !est
              ? '引擎未连接，无法判断与自动值的偏差'
              : drift
                ? '当前值与自动值偏差 >5%，引擎会在诊断中提示'
                : '与自动值一致'
          }
        >
          <span className="row">
            <Switch
              checked={fov.manual_override}
              onChange={(v) => update({ fov: { manual_override: v } })}
            />
            <Button
              size="small"
              disabled={!est}
              onClick={() =>
                update({
                  fov: {
                    manual_override: false,
                    hfov_deg: Number(autoH.toFixed(1)),
                    vfov_deg: Number(autoV.toFixed(1)),
                  },
                })
              }
            >
              回到自动值
            </Button>
          </span>
        </Field>
      </Section>
    </>
  );
}
