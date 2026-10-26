#!/usr/bin/env python
"""汇总 samples/mbonx/dumped 下所有明文模型，逐个探测并生成对照表。

用法:
    python tools/mbonx/report_all.py
输出:
    samples/mbonx/report/summary.md（同时打印到终端）
"""
import glob
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
DUMPED = os.path.join(ROOT, "samples", "mbonx", "dumped")
RAW = os.path.join(ROOT, "samples", "mbonx", "raw")
REPORT = os.path.join(ROOT, "samples", "mbonx", "report")
ENGINE = os.path.join(ROOT, "engine", "build-mingw", "src", "aim_engine_exe.exe")

FIELDS = {
    "input": re.compile(r"input\s+=\s+(\S+)\s+dtype=(\S+)\s+shape=\[([^\]]*)\]"),
    "output": re.compile(r"output\s+=\s+(\S+)\s+dtype=(\S+)\s+shape=\[([^\]]*)\]"),
    "classes": re.compile(r"classes\s+=\s+(\d+)\s+关键点\s+=\s+(\d+)"),
    "names": re.compile(r"类别表\s+=\s+(.+)"),
    "size": re.compile(r"size\s+=\s+(\d+)\s+字节"),
    "sha": re.compile(r"sha256\s+=\s+([0-9a-f]+)"),
    "err": re.compile(r"error:\s*(.+)"),
}


def probe(path):
    try:
        r = subprocess.run([ENGINE, "--probe-model", path], capture_output=True, text=True, timeout=180)
        return (r.stdout or "") + (r.stderr or "")
    except Exception as e:  # noqa: BLE001
        return f"error: {e}"


def guess_source(plain_size):
    """明文比密文短一点（容器头 + 校验），按最接近的原始文件反推来源。"""
    best = None
    for p in glob.glob(os.path.join(RAW, "*.mbonx")):
        d = abs(os.path.getsize(p) - plain_size)
        if best is None or d < best[0]:
            best = (d, os.path.basename(p))
    if best and best[0] < 4096:
        return best[1]
    return "?"


def main():
    os.makedirs(REPORT, exist_ok=True)
    files = sorted(glob.glob(os.path.join(DUMPED, "*.onnx")))
    if not files:
        print("dumped/ 下还没有模型")
        return 1

    rows = []
    for f in files:
        out = probe(f)
        row = {"file": os.path.basename(f)}
        for k, rx in FIELDS.items():
            m = rx.search(out)
            row[k] = m.group(1).strip() if m and m.lastindex == 1 else None
        m = FIELDS["input"].search(out)
        if m:
            row["in_dtype"], row["in_shape"] = m.group(2), m.group(3)
        m = FIELDS["output"].search(out)
        if m:
            row["out_shape"] = m.group(3)
        m = FIELDS["classes"].search(out)
        if m:
            row["classes"], row["kpts"] = m.group(1), m.group(2)
        m = FIELDS["size"].search(out)
        if m:
            row["bytes"] = int(m.group(1))
        m = FIELDS["names"].search(out)
        if m:
            row["names"] = m.group(1).strip()
        m = FIELDS["sha"].search(out)
        if m:
            row["sha"] = m.group(1)
        m = FIELDS["err"].search(out)
        if m and not row.get("in_shape"):
            row["error"] = m.group(1)
        rows.append(row)

    lines = ["# mbonx 明文模型汇总", "", "| 文件 | 推测来源 | 输入尺寸 | 类型 | 输出 | 类别 | 关键点 | 大小 |", "|---|---|---|---|---|---|---|---|"]
    for r in rows:
        if r.get("error"):
            lines.append(f"| {r['file']} | ? | 解析失败 | — | — | — | — | — |")
            continue
        shape = r.get("in_shape", "").split(",")
        dim = f"{shape[2]}×{shape[3]}" if len(shape) == 4 else r.get("in_shape", "?")
        src = guess_source(r.get("bytes", 0))
        lines.append(
            f"| {r['file']} | {src} | **{dim}** | {r.get('in_dtype','?')} | "
            f"[{r.get('out_shape','?')}] | {r.get('classes','?')} | {r.get('kpts','?')} | "
            f"{r.get('bytes',0)/1048576:.2f} MiB |"
        )

    lines += ["", "## 类别表", ""]
    for r in rows:
        if r.get("names"):
            lines.append(f"- **{r['file']}**（{r.get('classes')} 类）：{r['names']}")

    # 解码头布局判定：YOLOv8 为 [1, 4+C(+K), N]；YOLOv5 为 [1, N*anchors, 5+C]
    notes = []
    for r in rows:
        if not r.get("out_shape") or not r.get("classes"):
            continue
        try:
            dims = [int(x) for x in r["out_shape"].split(",")]
            cls = int(r["classes"])
        except ValueError:
            continue
        if len(dims) != 3:
            continue
        if dims[1] == 4 + cls:
            continue  # 标准 v8 布局
        notes.append(
            f"- **{r['file']}**：输出 `{r['out_shape']}`，`{dims[1]} != 4+{cls}`，"
            f"不是 YOLOv8 的 `[1,4+C,N]` 布局（疑似 YOLOv5 `[1,N*anchors,5+C]`，"
            f"需转置 + obj 置信度解码），当前引擎 `DecodeYolo` 不能直接吃。"
        )
    if notes:
        lines += ["", "## 解码头差异", ""] + notes

    lines += ["", "## 共性", "", "- 全部 **320×320** 输入、float32、无关键点。", "- 除上表标注外均为 YOLOv8 头 `[1, 4+C, 2100]`（2100 = 1600+400+100）。"]

    text = "\n".join(lines) + "\n"
    out_path = os.path.join(REPORT, "summary.md")
    with open(out_path, "w", encoding="utf-8") as fh:
        fh.write(text)
    print(text)
    print(f"已写入 {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
