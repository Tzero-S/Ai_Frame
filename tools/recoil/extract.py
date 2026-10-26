#!/usr/bin/env python
"""弹道录制：逐帧画面 → 后坐力曲线 → patterns.json 条目。

输入
----
  --video   视频文件（mp4/mkv/flv…），用 ffmpeg 抽帧；中心自动裁剪到 ROI
  --frames  已导出的帧目录（png/jpg/bmp）
ffmpeg 查找顺序：--ffmpeg → 环境变量 AIM_FFMPEG → 本机已知路径 → PATH。

输入
----
  --video   视频文件（mp4/mkv/flv…），用 ffmpeg 抽帧；中心自动裁剪到 ROI
  --frames  已导出的帧目录（png/jpg/bmp）
ffmpeg 查找顺序：--ffmpeg → 环境变量 AIM_FFMPEG → 本机已知路径 → PATH。

原理
----
后坐力表现为「视角上跳」，在画面上等价于「背景整体下移」。用相位相关
（phase correlation）逐对相邻帧估计纯平移量，累加得到轨迹，即为弹道形状。

两种录制口径：
  drift（默认）第一人称：准星固定在屏幕中心，画面内容在动 → 位移取反
  shift        准星在画面中移动（第三人称 / 带准星位移）→ 位移直接使用

用法
----
    python tools/recoil/extract.py --video rec.mp4 --id ak47 --rpm 600
    python tools/recoil/extract.py --frames ./rec --fps 60 --id ak47 --rpm 600
    python tools/recoil/extract.py --frames ./rec --out data/patterns.json
    python tools/recoil/extract.py --self-test          # 合成数据自检
    python tools/recoil/extract.py --frames ./rec --dry-run

输出为归一化形状：dy 峰值 = 1.0，实际补偿 = strength × k × shape(t)。
"""
import argparse
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

IMG_EXT = ("*.png", "*.jpg", "*.jpeg", "*.bmp")
MAX_FRAMES = 5000

# 本机已知位置（找不到时会回落到 PATH）
FFMPEG_CANDIDATES = (
    r"D:/MyProject/Test.Project/test/projects/VALORANT_aimbot/tools/ffmpeg.exe",
)


def find_ffmpeg(explicit=None):
    """按 显式参数 → 环境变量 → 已知路径 → PATH 的顺序定位 ffmpeg。"""
    if explicit:
        if not os.path.isfile(explicit):
            raise ValueError(f"--ffmpeg 指定的文件不存在: {explicit}")
        return explicit
    env = os.environ.get("AIM_FFMPEG", "")
    if env and os.path.isfile(env):
        return env
    for cand in FFMPEG_CANDIDATES:
        if os.path.isfile(cand):
            return cand
    found = shutil.which("ffmpeg")
    if found:
        return found
    raise ValueError("未找到 ffmpeg：用 --ffmpeg 指定，或设置环境变量 AIM_FFMPEG，或加入 PATH")


def probe_video(ffmpeg, path):
    """解析 ffmpeg -i 的 stderr，取出 (宽, 高, 帧率)。"""
    if not os.path.isfile(path):
        raise ValueError(f"视频不存在: {path}")
    # ffmpeg -i 只探测不输出，非零退出属正常
    proc = subprocess.run([ffmpeg, "-hide_banner", "-i", path],
                          capture_output=True, text=True, errors="replace")
    text = (proc.stderr or "") + (proc.stdout or "")
    m = re.search(r"Video:.*?,\s*(\d{2,5})x(\d{2,5})", text)
    if not m:
        raise ValueError(f"无法从视频中解析分辨率（可能不是视频文件）: {path}")
    w, h = int(m.group(1)), int(m.group(2))
    fm = re.search(r"([\d.]+)\s*fps", text)
    src_fps = float(fm.group(1)) if fm else 0.0
    return w, h, src_fps


def grab_frames(ffmpeg, video, out_dir, roi, fps, start=None, duration=None, max_frames=None,
                y_offset=0, src_w=0, src_h=0):
    """用 ffmpeg 抽取中心 roi×roi 的帧，返回帧文件列表。"""
    cmd = [ffmpeg, "-hide_banner", "-loglevel", "error", "-y"]
    if start is not None:
        cmd += ["-ss", f"{start}"]
    if duration is not None:
        cmd += ["-t", f"{duration}"]
    if src_w > 0 and src_h > 0:
        x0 = max(0, (src_w - roi) // 2)
        y0 = max(0, min(src_h - roi, (src_h - roi) // 2 + int(y_offset)))
        crop = f"crop={roi}:{roi}:{x0}:{y0}"
    else:
        crop = f"crop={roi}:{roi}"
    cmd += ["-i", video, "-vf", f"fps={fps},{crop}"]
    if max_frames:
        cmd += ["-frames:v", str(int(max_frames))]
    cmd += [os.path.join(out_dir, "f%05d.png")]

    proc = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
    if proc.returncode != 0:
        tail = (proc.stderr or "").strip().splitlines()[-3:]
        raise ValueError("ffmpeg 抽帧失败: " + " | ".join(tail))
    frames = sorted(glob.glob(os.path.join(out_dir, "*.png")))
    if not frames:
        raise ValueError("ffmpeg 未产出任何帧（检查 --start/--duration 是否超出视频范围）")
    return frames


# ---------------------------------------------------------------- 图像


def load_gray(path, roi, y_offset=0):
    """读图并取 roi×roi 灰度块（默认居中，y_offset 负数上移）。返回 (数组, 源图尺寸)。"""
    with Image.open(path) as im:
        g = im.convert("L")
        w, h = g.size
        if roi > min(w, h):
            raise ValueError(f"{os.path.basename(path)}: 图像 {w}x{h} 小于 ROI {roi}")
        x0 = (w - roi) // 2
        y0 = (h - roi) // 2 + int(y_offset)
        y0 = max(0, min(h - roi, y0))  # 越界收敛，避免 PIL 补黑边
        arr = np.asarray(g.crop((x0, y0, x0 + roi, y0 + roi)), dtype=np.float32)
    arr -= arr.mean()
    return arr, (w, h)


def _parabolic(cm1, c0, cp1):
    """三点抛物线顶点偏移，用于亚像素修正；分母过小时退化为 0。"""
    denom = cm1 - 2.0 * c0 + cp1
    if abs(denom) < 1e-12:
        return 0.0
    return float(np.clip(0.5 * (cm1 - cp1) / denom, -0.5, 0.5))


def phase_shift(a, b):
    """估计 b 相对 a 的平移 (dx, dy)，含亚像素修正。"""
    fa = np.fft.rfft2(a)
    fb = np.fft.rfft2(b)
    cross = fa * np.conj(fb)
    mag = np.abs(cross)
    mag[mag < 1e-9] = 1e-9
    corr = np.fft.irfft2(cross / mag, s=a.shape).real
    py, px = np.unravel_index(int(np.argmax(corr)), corr.shape)
    h, w = corr.shape
    sub_y = _parabolic(corr[(py - 1) % h, px], corr[py, px], corr[(py + 1) % h, px])
    sub_x = _parabolic(corr[py, (px - 1) % w], corr[py, px], corr[py, (px + 1) % w])
    if py > h // 2:
        py -= h
    if px > w // 2:
        px -= w
    return float(px) + sub_x, float(py) + sub_y


# ---------------------------------------------------------------- 提取


def collect_frames(frames_dir):
    files = []
    for ext in IMG_EXT:
        files.extend(glob.glob(os.path.join(frames_dir, ext)))
    files = sorted(set(files))
    if len(files) > MAX_FRAMES:
        raise ValueError(f"帧数 {len(files)} 超过上限 {MAX_FRAMES}")
    return files


def track_px(frames, roi, fps, mode, y_offset=0):
    """逐帧估计位移，返回 [(t_ms, dx_px, dy_px)]（绝对像素，未归一化）。"""
    if len(frames) < 2:
        raise ValueError("至少需要 2 帧")
    if fps <= 0:
        raise ValueError("fps 必须为正")

    prev, src_size = load_gray(frames[0], roi, y_offset)
    shape = prev.shape
    sign = -1.0 if mode == "drift" else 1.0
    cx = cy = 0.0
    pts = [(0.0, 0.0, 0.0)]

    for i, path in enumerate(frames[1:], start=1):
        cur, cur_size = load_gray(path, roi, y_offset)
        if cur_size != src_size:
            raise ValueError(
                f"{os.path.basename(path)}: 分辨率 {cur_size[0]}x{cur_size[1]} "
                f"与首帧 {src_size[0]}x{src_size[1]} 不一致")
        if cur.shape != shape:
            raise ValueError(f"{os.path.basename(path)}: ROI 尺寸异常")
        dx, dy = phase_shift(prev, cur)
        cx += sign * dx
        cy += sign * dy
        pts.append((i * 1000.0 / fps, cx, cy))
        prev = cur
    return pts


def best_segment(pts, fps, min_rise_px, max_dur_s=4.0):
    """在绝对轨迹里找"最佳连发窗口"：持续上跳、单调、幅度足够。

    整段归一化会被一次视角拉回主导（真实素材里很常见），
    所以必须先裁出连发段，再归一化。
    返回 (start_idx, end_idx, rise_px, mono_ratio)；找不到返回 None。
    """
    n = len(pts)
    if n < 4:
        return None
    ys = [p[2] for p in pts]
    # 平滑（约 50ms），抑制单帧噪声
    k = max(1, int(round(fps * 0.05)))
    sm = []
    for i in range(n):
        a = max(0, i - k // 2)
        b = min(n, i + k // 2 + 1)
        sm.append(sum(ys[a:b]) / (b - a))

    best = None
    min_len = max(2, int(round(fps * 0.8)))          # 至少 0.8s
    max_len = max(min_len, int(round(fps * max_dur_s)))
    for length in range(min_len, min(n, max_len) + 1):
        for s0 in range(0, n - length + 1):
            s1 = s0 + length
            rise = sm[s1 - 1] - sm[s0]
            if rise < min_rise_px:
                continue
            seg = sm[s0:s1]
            # 单调性：相邻帧基本不回落（允许 5% 幅度噪声）
            ups = sum(1 for i in range(1, len(seg)) if seg[i] >= seg[i - 1] - 0.02 * rise)
            mono = ups / (len(seg) - 1)
            score = rise * mono
            if best is None or score > best[0]:
                best = (score, s0, s1, rise, mono)
    if not best:
        return None
    return best[1], best[2], best[3], best[4]


def normalize(pts, start=0, end=None):
    """按窗口内峰值归一化（dy 峰值 = 1.0）。"""
    end = len(pts) if end is None else end
    seg = pts[start:end]
    if not seg:
        return []
    t0 = seg[0][0]
    peak = max(abs(p[2]) for p in seg)
    if peak < 1e-6:
        raise ValueError("窗口内没有位移")
    return [(p[0] - t0, p[1] / peak, p[2] / peak) for p in seg]


def extract(frames, roi, fps, mode, y_offset=0, auto_segment=True, min_rise_px=60.0):
    """完整流程：轨迹 → 自动分段 → 归一化。返回 (samples, info)。"""
    pts = track_px(frames, roi, fps, mode, y_offset)
    info = {"raw_points": len(pts), "segmented": False, "rise_px": 0.0, "mono": 0.0}

    if auto_segment:
        seg = best_segment(pts, fps, min_rise_px)
        if seg is None:
            # 找不到合格连发段：退回整段，但明确告知调用方
            info["warning"] = (f"未找到累计上跳 ≥ {min_rise_px:.0f}px 的连发段，"
                               f"已退回整段（曲线可能被视角移动污染）")
            return normalize(pts), info
        s0, s1, rise, mono = seg
        info.update({"segmented": True, "start_ms": pts[s0][0], "end_ms": pts[s1 - 1][0],
                     "rise_px": rise, "mono": mono})
        return normalize(pts, s0, s1), info

    return normalize(pts), info


def _r(v, nd=4):
    """四舍五入并消除 -0.0（JSON 里虽然合法，但会让下游比较出现意外）。"""
    x = round(float(v), nd)
    return 0.0 if x == 0 else x


def to_pattern(pts, pid, name, rpm):
    return {
        "id": pid,
        "name": name,
        "rpm": int(rpm),
        "samples": [
            {"t": int(round(t)), "dx": _r(dx), "dy": _r(dy)} for t, dx, dy in pts
        ],
    }


# ---------------------------------------------------------------- 落盘


def merge_into(path, pattern):
    """写入 patterns.json：同 id 覆盖，其余保留；文件不存在则新建。"""
    data = {"version": 1, "patterns": []}
    if os.path.exists(path):
        try:
            with open(path, encoding="utf-8") as f:
                data = json.load(f)
        except (OSError, json.JSONDecodeError) as e:
            raise ValueError(f"已有 patterns.json 无法解析: {e}") from e
        if not isinstance(data.get("patterns"), list):
            raise ValueError("已有 patterns.json 缺少 patterns 数组")
    data["patterns"] = [p for p in data["patterns"] if p.get("id") != pattern["id"]]
    data["patterns"].append(pattern)
    data["patterns"].sort(key=lambda p: p.get("id", ""))
    os.makedirs(os.path.dirname(os.path.abspath(path)) or ".", exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
        f.write("\n")
    os.replace(tmp, path)  # 原子替换，避免写一半
    return len(data["patterns"])


# ---------------------------------------------------------------- 自检


def self_test(roi=192, n=40, fps=60):
    """合成已知轨迹的帧序列，验证还原精度。"""
    import tempfile

    rng = np.random.default_rng(7)
    big = rng.integers(0, 255, size=(roi * 4, roi * 4), dtype=np.uint8)
    base = Image.fromarray(big, mode="L")

    # 已知轨迹：先快后慢的垂直上跳 + 轻微水平漂移（模拟真实弹道形状）。
    # 注意：像素只能整数量化，因此"真值"记录量化后的位移，
    # 否则测到的是生成器的取整误差，而不是提取器的误差。
    truth = []
    cy = cx = 0.0
    for i in range(n):
        # 先记录当前累计位移，再叠加下一帧增量：
        # 第 0 帧即"开火前"的零点，与提取器以首帧为基准的口径一致。
        truth.append((i * 1000.0 / fps, float(round(cx)), float(round(cy))))
        t = i / fps
        cy += 3.0 * np.exp(-t * 2.0) + 0.8
        cx += 0.8 * np.sin(t * 6.0)

    with tempfile.TemporaryDirectory() as td:
        for i, (_, x, y) in enumerate(truth):
            # 后坐力 = 视角上跳 = 画面内容下移 → 取样窗口向上移动
            ox = roi + int(x)
            oy = roi - int(y)
            crop = base.crop((ox, oy, ox + roi, oy + roi))
            crop.save(os.path.join(td, f"f{i:04d}.png"))

        frames = collect_frames(td)
        got, _info = extract(frames, roi, fps, mode="drift")

    # 两条曲线都归一到峰值 1.0 后比较
    def norm(pts):
        peak = max(abs(p[2]) for p in pts)
        return [p[2] / peak for p in pts]

    a = norm(truth)
    b = norm(got)
    m = min(len(a), len(b))
    err = max(abs(a[i] - b[i]) for i in range(m))
    ok = err < 0.05
    print(f"[self-test] 帧数={n} ROI={roi} 最大归一化误差={err:.4f} "
          f"→ {'PASS' if ok else 'FAIL'}（阈值 0.05）")
    return 0 if ok else 1


# ---------------------------------------------------------------- 入口


def main():
    ap = argparse.ArgumentParser(description="弹道录制：帧序列 → patterns.json")
    ap.add_argument("--frames", help="帧目录（按文件名排序）")
    ap.add_argument("--video", help="视频文件（用 ffmpeg 抽帧）")
    ap.add_argument("--ffmpeg", default="", help="ffmpeg 路径（默认自动查找）")
    ap.add_argument("--start", type=float, default=None, help="视频起点（秒）")
    ap.add_argument("--duration", type=float, default=None, help="视频时长（秒）")
    ap.add_argument("--max-frames", type=int, default=0, help="最多取多少帧（0=不限）")
    ap.add_argument("--keep-frames", default="", help="保留抽出的帧到该目录")
    ap.add_argument("--y-offset", type=int, default=0,
                    help="取样框纵向偏移（px，负数上移）；第一人称建议上移避开枪身")
    ap.add_argument("--no-segment", action="store_true",
                    help="关闭自动分段（默认开）：整段归一化，视角移动会污染曲线")
    ap.add_argument("--min-rise", type=float, default=60.0,
                    help="自动分段要求的最小累计上跳（px），默认 60")
    ap.add_argument("--fps", type=float, default=None,
                    help="帧率；--video 模式下默认取视频原始帧率，--frames 模式默认 60")
    ap.add_argument("--roi", type=int, default=320, help="中心取样边长，默认 320")
    ap.add_argument("--mode", choices=("drift", "shift"), default="drift",
                    help="drift=第一人称(画面动，取反)；shift=准星位移")
    ap.add_argument("--id", default="", help="弹道 id，如 ak47")
    ap.add_argument("--name", default="", help="显示名")
    ap.add_argument("--rpm", type=int, default=600, help="射速")
    ap.add_argument("--out", default="", help="写入的 patterns.json（留空=只打印）")
    ap.add_argument("--dry-run", action="store_true", help="只打印不写文件")
    ap.add_argument("--self-test", action="store_true", help="合成数据自检")
    args = ap.parse_args()

    if args.self_test:
        return self_test()

    if not args.frames and not args.video:
        ap.error("需要 --frames、--video 或 --self-test")
    if not args.id:
        ap.error("需要 --id（弹道标识）")
    if args.frames and args.video:
        ap.error("--frames 与 --video 只能二选一")

    tmp_dir = ""
    try:
        if args.video:
            ffmpeg = find_ffmpeg(args.ffmpeg or None)
            w, h, src_fps = probe_video(ffmpeg, args.video)
            fps = args.fps if args.fps else (src_fps or 60.0)
            if fps <= 0:
                raise ValueError("fps 必须为正")
            # 取样框不能超过画面，且 H.264 的 yuv420 要求偶数边长
            roi = min(args.roi, w, h)
            roi -= roi % 2
            if roi < 16:
                raise ValueError(f"视频 {w}x{h} 太小，无法取 {args.roi} 的取样框")
            tmp_dir = args.keep_frames or tempfile.mkdtemp(prefix="aim_recoil_")
            os.makedirs(tmp_dir, exist_ok=True)
            frames = grab_frames(ffmpeg, args.video, tmp_dir, roi, fps,
                                 args.start, args.duration, args.max_frames,
                                 args.y_offset, w, h)
            print(f"视频 {w}x{h} @{src_fps:.3g}fps → 抽帧 {len(frames)} 张，取样 {roi}px")
        else:
            if not os.path.isdir(args.frames):
                raise ValueError(f"目录不存在: {args.frames}")
            fps = args.fps if args.fps else 60.0
            roi = args.roi
            frames = collect_frames(args.frames)

        pts, info = extract(frames, roi, fps, args.mode,
                            y_offset=0 if args.video else args.y_offset,
                            auto_segment=not args.no_segment,
                            min_rise_px=args.min_rise)
    except ValueError as e:
        print(f"错误: {e}")
        return 2
    finally:
        if tmp_dir and not args.keep_frames and os.path.isdir(tmp_dir):
            shutil.rmtree(tmp_dir, ignore_errors=True)

    pat = to_pattern(pts, args.id, args.name or args.id, args.rpm)
    if info.get("segmented"):
        print(f"自动分段：{info['start_ms']:.0f}–{info['end_ms']:.0f}ms"
              f"（{info['end_ms']-info['start_ms']:.0f}ms，上跳 {info['rise_px']:.0f}px，"
              f"单调 {info['mono']*100:.0f}%）")
    if info.get("warning"):
        print(f"⚠️  {info['warning']}")
    print(f"帧数={len(pts)} 时长={pts[-1][0]:.0f}ms "
          f"dy峰值=1.0 dx峰值={max(abs(p[1]) for p in pts):.3f}")

    if args.dry_run or not args.out:
        print(json.dumps(pat, ensure_ascii=False, indent=2)[:800])
        return 0

    try:
        total = merge_into(args.out, pat)
    except ValueError as e:
        print(f"错误: {e}")
        return 2
    print(f"已写入 {args.out}（当前共 {total} 条）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
