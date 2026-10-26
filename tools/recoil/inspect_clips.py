#!/usr/bin/env python
"""素材体检：批量提取弹道并判定可用性。

判定标准（第一人称、零压枪录制时应满足）：
  1. dy 在前 0.5–1s 内快速上升，之后趋缓（单调上升占比高）
  2. 相邻帧位移平稳，无突跳（视角被手动转动会表现为大跳）
  3. dx 幅度远小于 dy（纯垂直后坐力）
"""
import glob
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
EXTRACT = os.path.join(ROOT, "tools", "recoil", "extract.py")
OUT_DIR = os.path.join(ROOT, "samples", "recoil", "raw")


def run(clip, pid, roi=800, fps=60):
    os.makedirs(OUT_DIR, exist_ok=True)
    out = os.path.join(OUT_DIR, f"{pid}.json")
    cmd = [sys.executable, EXTRACT, "--video", clip, "--id", pid,
           "--roi", str(roi), "--fps", str(fps), "--out", out]
    r = subprocess.run(cmd, capture_output=True, text=True, errors="replace", timeout=600)
    if r.returncode != 0:
        return None, (r.stdout or "") + (r.stderr or "")
    seg = [l for l in (r.stdout or "").splitlines() if "自动分段" in l or "⚠️" in l]
    return {"seg": seg, "out": out}, None


def analyze(samples):
    ys = [s["dy"] for s in samples]
    xs = [s["dx"] for s in samples]
    ts = [s["t"] for s in samples]
    n = len(ys)
    if n < 10:
        return None

    # 单调上升占比（允许 0.002 的回退噪声）
    ups = sum(1 for i in range(1, n) if ys[i] >= ys[i - 1] - 0.002)
    mono = ups / (n - 1)

    # 相邻帧最大跳变（视角被转动会表现为大跳）
    jumps = [abs(ys[i] - ys[i - 1]) for i in range(1, n)]
    max_jump = max(jumps) if jumps else 0.0

    # 前 1s 内完成的比例
    t1 = 1000.0
    idx1 = max(1, min(n - 1, next((i for i, t in enumerate(ts) if t > t1), n - 1)))
    early = ys[idx1]

    # 水平占比
    dx_amp = max(abs(v) for v in xs) if xs else 0.0

    return {
        "n": n,
        "dur_ms": ts[-1],
        "mono": mono,
        "max_jump": max_jump,
        "early_1s": early,
        "dx_amp": dx_amp,
        "tail_flat": abs(ys[-1] - ys[int(n * 0.8)]) < 0.05,
    }


def verdict(a):
    """可用性判定。"""
    if a["mono"] < 0.90:
        return "❌ 不可用（dy 反复回落 → 录制时视角在动）"
    if a["max_jump"] > 0.25:
        return "❌ 不可用（存在大跳 → 视角被转动/切枪）"
    if a["dx_amp"] > 0.8:
        return "⚠️ 存疑（水平分量过大 → 可能在横扫）"
    if a["early_1s"] < 0.6:
        return "⚠️ 存疑（1s 内上升不足 60% → 可能不是一梭子后坐力）"
    return "✅ 可用"


def main():
    clips = sorted(glob.glob(r"D:/N卡/Delta Force/Delta Force 2026.10.26*.mp4"))
    if not clips:
        print("未找到素材")
        return 2
    print(f"共 {len(clips)} 段素材\n")
    rows = []
    for i, clip in enumerate(clips, 1):
        pid = f"clip{i:02d}"
        print(f"[{i}/{len(clips)}] {os.path.basename(clip)}")
        samples, err = run(clip, pid)
        if not samples:
            print(f"    提取失败: {err[:200]}")
            continue
        a = analyze(samples)
        if not a:
            print("    样本过少")
            continue
        v = verdict(a)
        print(f"    {a['n']} 帧 / {a['dur_ms']:.0f} ms | 单调 {a['mono']*100:.1f}% | "
              f"最大跳变 {a['max_jump']:.3f} | 1s内 {a['early_1s']:.2f} | "
              f"dx {a['dx_amp']:.2f}")
        print(f"    {v}")
        rows.append((pid, os.path.basename(clip), a, v))

    print("\n=== 汇总 ===")
    ok = [r for r in rows if r[3].startswith("✅")]
    maybe = [r for r in rows if r[3].startswith("⚠️")]
    bad = [r for r in rows if r[3].startswith("❌")]
    print(f"可用 {len(ok)} / 存疑 {len(maybe)} / 不可用 {len(bad)}")
    for pid, name, a, v in rows:
        print(f"  {pid}  {v}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
