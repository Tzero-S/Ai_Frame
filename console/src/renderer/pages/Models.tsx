import { useRef, useState } from 'react';
import { Alert, Button, Checkbox, Input, Slider, Tag, message } from 'antd';
import { Field, Section } from '../components/ui';
import { invoke } from '../ipc';
import type { Detection, IpcResponse, ModelInfo } from '../types';
import type { PageProps } from './common';

const DEFAULT_MODEL =
  'D:/MyProject/Test.Project/Ai_Frame/samples/mbonx/dumped/model_37814619.onnx';

const PALETTE = ['#52C41A', '#F5222D', '#1677FF', '#FA8C16', '#722ED1', '#13C2C2', '#EB2F96'];

function bgraBase64(data: Uint8ClampedArray): string {
  const bytes = new Uint8Array(data.length);
  for (let i = 0; i < data.length; i += 4) {
    bytes[i] = data[i + 2];
    bytes[i + 1] = data[i + 1];
    bytes[i + 2] = data[i];
    bytes[i + 3] = 255;
  }
  let bin = '';
  const chunk = 0x2000;
  for (let i = 0; i < bytes.length; i += chunk) {
    bin += String.fromCharCode(...bytes.subarray(i, i + chunk));
  }
  return btoa(bin);
}

export function Models({ cfg, update }: PageProps) {
  const [path, setPath] = useState(DEFAULT_MODEL);
  const [info, setInfo] = useState<ModelInfo | null>(null);
  const [busy, setBusy] = useState(false);
  const [conf, setConf] = useState(cfg.inference.conf_threshold);
  const [detections, setDetections] = useState<Detection[]>([]);
  const [rawCount, setRawCount] = useState(0);
  const [inferMs, setInferMs] = useState(0);
  const canvasRef = useRef<HTMLCanvasElement | null>(null);

  // v4：选中项存"名字"；未选 = 交给引擎自动挑选
  const selected = cfg.inference.selected_class_names;
  const toggleClass = (name: string, on: boolean) => {
    const next = on ? [...selected.filter((n) => n !== name), name] : selected.filter((n) => n !== name);
    update({ inference: { selected_class_names: next } });
  };

  // 预览用的索引白名单：按名字在模型类别里定位；一个都没选则传空（= 全部）
  const previewIndices = (): number[] => {
    if (!info || selected.length === 0) return [];
    const out: number[] = [];
    info.classes.forEach((name, idx) => {
      if (selected.includes(name)) out.push(idx);
    });
    return out;
  };

  const importModel = async () => {
    setBusy(true);
    try {
      const r: IpcResponse = await invoke('model.import', { path });
      if (r.code !== 0) throw new Error(r.msg);
      const payload = r.payload as { model?: ModelInfo } | undefined;
      const m = payload?.model ?? null;
      setInfo(m);
      // 把模型真实类别回写到配置，供引擎与其它页面共用
      if (m?.classes?.length) {
        update({ inference: { class_names: m.classes } });
      }
      message.success('模型校验通过');
    } catch (e) {
      setInfo(null);
      message.error(e instanceof Error ? e.message : '导入失败');
    } finally {
      setBusy(false);
    }
  };

  const preview = async (file: File) => {
    setBusy(true);
    try {
      const url = URL.createObjectURL(file);
      const img = new Image();
      img.src = url;
      await img.decode();

      // 按模型实际输入边长取样，避免把 960 的图喂给 320 的模型
      const shape = info?.input.shape ?? [];
      const side = shape.length === 4 && shape[2] > 0 ? Number(shape[2]) : 960;

      const canvas = document.createElement('canvas');
      canvas.width = side;
      canvas.height = side;
      const ctx = canvas.getContext('2d');
      if (!ctx) throw new Error('canvas unavailable');
      ctx.drawImage(img, 0, 0, side, side);
      const data = ctx.getImageData(0, 0, side, side).data;
      const b64 = bgraBase64(data);

      const r = await invoke('model.preview', {
        model_path: path,
        width: side,
        height: side,
        bgra_base64: b64,
        conf,
        iou: cfg.inference.iou_threshold,
        classes: previewIndices(),
      });
      if (r.code !== 0) throw new Error(r.msg);

      const p = r.payload as
        | { detections?: Detection[]; raw_count?: number; infer_ms?: number }
        | undefined;
      const dets = p?.detections ?? [];
      setDetections(dets);
      setRawCount(p?.raw_count ?? 0);
      setInferMs(p?.infer_ms ?? 0);

      const view = canvasRef.current;
      if (view) {
        view.width = 480;
        view.height = 480;
        const vctx = view.getContext('2d');
        if (vctx) {
          vctx.drawImage(img, 0, 0, 480, 480);
          vctx.lineWidth = 2;
          vctx.font = '12px Consolas, monospace';
          const scale = 480 / side;
          for (const d of dets) {
            const color = PALETTE[d.class_id % PALETTE.length];
            vctx.strokeStyle = color;
            vctx.strokeRect(d.x * scale, d.y * scale, d.w * scale, d.h * scale);
            vctx.fillStyle = color;
            const label = info?.classes[d.class_id] ?? `#${d.class_id}`;
            vctx.fillText(
              `${label} ${d.score.toFixed(2)}`,
              d.x * scale,
              Math.max(10, d.y * scale - 4),
            );
          }
        }
      }
      URL.revokeObjectURL(url);
    } catch (e) {
      message.error(e instanceof Error ? e.message : '预览失败');
    } finally {
      setBusy(false);
    }
  };

  return (
    <>
      <Section
        title="模型导入"
        extra={
          <Button size="small" loading={busy} onClick={() => void importModel()}>
            导入并校验
          </Button>
        }
      >
        <Field label="模型路径" desc=".onnx（ORT / DML）">
          <Input
            style={{ width: 520 }}
            value={path}
            onChange={(e) => setPath(e.target.value)}
          />
        </Field>
        {info ? (
          <div style={{ padding: '8px 0' }}>
            <div className="row" style={{ gap: 8, flexWrap: 'wrap' }}>
              <Tag color="blue">{info.alias}</Tag>
              <Tag>{info.num_classes} 类</Tag>
              <Tag>{info.classes.join(' / ') || '未声明类别'}</Tag>
              <Tag color={info.input.dynamic ? 'orange' : 'green'}>
                {info.input.dynamic ? '动态输入' : '静态输入'}
              </Tag>
            </div>
            <div className="mono" style={{ marginTop: 6 }}>
              input {info.input.name} [{info.input.shape.join(', ')}] · output{' '}
              {info.output.name} [{info.output.shape.join(', ')}]
            </div>
            <div className="mono">sha256 {info.sha256}</div>
          </div>
        ) : null}
        {info && info.classes.length > 0 ? (
          <Field
            label="瞄准类别"
            desc="按名字保存，换模型不会错位；一个都不选 = 引擎自动挑选（body/head 类，排除队友/倒地/靶场）"
          >
            <div style={{ display: 'flex', flexWrap: 'wrap', gap: '6px 14px', maxWidth: 460 }}>
              {info.classes.map((name, idx) => (
                <Checkbox
                  key={`${idx}-${name}`}
                  checked={selected.includes(name)}
                  onChange={(e) => toggleClass(name, e.target.checked)}
                >
                  {name}
                </Checkbox>
              ))}
            </div>
          </Field>
        ) : null}
      </Section>

      <Section
        title="静态图预览"
        extra={<span className="hint">与生产共用同一 backend / 后处理</span>}
      >
        <Field label="置信度阈值">
          <Slider
            style={{ width: 220 }}
            min={0.05}
            max={0.9}
            step={0.05}
            value={conf}
            onChange={setConf}
          />
        </Field>
        <Field label="选择图片">
          <input
            type="file"
            accept="image/*"
            onChange={(e) => {
              const f = e.target.files?.[0];
              if (f) void preview(f);
            }}
          />
        </Field>
        <div className="row" style={{ gap: 12, margin: '8px 0' }}>
          <Tag>raw {rawCount}</Tag>
          <Tag color="blue">detections {detections.length}</Tag>
          <Tag>infer {inferMs.toFixed(1)} ms</Tag>
        </div>
        <canvas ref={canvasRef} className="preview-canvas" width={480} height={480} />
        {!info ? (
          <Alert
            style={{ marginTop: 8 }}
            type="info"
            showIcon
            message="先导入模型，再选择图片预览"
          />
        ) : null}
      </Section>
    </>
  );
}
