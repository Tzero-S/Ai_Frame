#!/usr/bin/env python
"""内存扫描 dump：样本已经把模型加载进 ORT 时使用。

流程：
  1. attach 到运行中的样本进程
  2. 扫描 ONNX protobuf 特征（12 07 "pytorch"）
  3. 从命中点向前退 2 字节作为起点，按 1MB 分块读回
  4. 用 protobuf 线格式走到模型真实结尾并裁剪
  5. 交给引擎 --probe-model 校验，通过才落盘

用法:
    python tools/mbonx/scan_dump.py --pid 45428
    python tools/mbonx/scan_dump.py --pid 45428 --max-hits 6
"""
import argparse
import datetime as dt
import os
import subprocess
import sys

import frida

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT_DIR = os.path.join(ROOT, "samples", "mbonx", "dumped")
LOG_DIR = os.path.join(ROOT, "samples", "logs")
ENGINE = os.path.join(ROOT, "engine", "build-mingw", "src", "aim_engine_exe.exe")


def log(msg):
    line = f"[{dt.datetime.now():%H:%M:%S}] {msg}"
    print(line, flush=True)
    with open(os.path.join(LOG_DIR, "scan.log"), "a", encoding="utf-8") as f:
        f.write(line + "\n")


def read_varint(buf, i):
    """返回 (value, next_index)；非法返回 (None, i)。"""
    shift = 0
    val = 0
    while i < len(buf) and shift < 64:
        b = buf[i]
        val |= (b & 0x7F) << shift
        i += 1
        if not (b & 0x80):
            return val, i
        shift += 7
    return None, i


def walk_protobuf(buf, start=0, limit=None):
    """按 protobuf 线格式走到消息结尾，返回 (end, boundaries[])。"""
    i = start
    end_limit = len(buf) if limit is None else min(limit, len(buf))
    boundaries = []
    while i < end_limit:
        key, ni = read_varint(buf, i)
        if key is None or ni == i:
            break
        field = key >> 3
        wire = key & 7
        if field == 0 or wire not in (0, 1, 2, 5):
            break
        j = ni
        if wire == 0:
            _, j = read_varint(buf, j)
            if j > end_limit:
                break
        elif wire == 1:
            j += 8
        elif wire == 5:
            j += 4
        else:  # wire == 2
            ln, j2 = read_varint(buf, j)
            if ln is None:
                break
            j = j2 + ln
        if j > end_limit or j <= i:
            break
        i = j
        boundaries.append(i)
    return i, boundaries


def validate(path):
    """用引擎校验是否为可解析 ONNX。"""
    try:
        r = subprocess.run([ENGINE, "--probe-model", path], capture_output=True, text=True, timeout=120)
        out = (r.stdout or "") + (r.stderr or "")
        return r.returncode == 0 and "格式 = onnx" in out, out
    except Exception as e:  # noqa: BLE001
        return False, str(e)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pid", type=int, required=True)
    ap.add_argument("--max-hits", type=int, default=6)
    ap.add_argument("--agent", default=os.path.join(HERE, "scan_dump.js"))
    args = ap.parse_args()

    os.makedirs(OUT_DIR, exist_ok=True)
    os.makedirs(LOG_DIR, exist_ok=True)

    with open(args.agent, encoding="utf-8") as f:
        js = f.read()

    device = frida.get_local_device()
    session = device.attach(args.pid)
    script = session.create_script(js)

    chunks = {}   # id -> {off: bytes}
    totals = {}

    def on_message(message, data):
        if message.get("type") == "error":
            log(f"[!] 脚本错误: {message.get('description')}")
            return
        p = message.get("payload") or {}
        t = p.get("type")
        if t == "log":
            log(p.get("msg", ""))
        elif t == "chunk" and data:
            chunks.setdefault(p["id"], {})[p["off"]] = bytes(data)
        elif t == "done":
            totals[p["id"]] = p["total"]
            log(f"[id {p['id']}] 读取完成，共 {p['total']} 字节")

    script.on("message", on_message)
    script.load()

    n = script.exports_sync.scananddump(args.max_hits)
    log(f"[*] 扫描结束，命中 {n} 处")

    saved = []
    for cid in sorted(chunks.keys()):
        parts = chunks[cid]
        buf = b"".join(parts[o] for o in sorted(parts.keys()))
        log(f"[id {cid}] 拼装 {len(buf)} 字节，开始裁剪")

        end, boundaries = walk_protobuf(buf)
        candidates = [end] + list(reversed(boundaries[-24:]))
        ok = False
        for k, cand in enumerate(candidates):
            if cand < 4096:
                continue
            path = os.path.join(OUT_DIR, f"scan_{cid:02d}_try{k}.onnx")
            with open(path, "wb") as f:
                f.write(buf[:cand])
            good, out = validate(path)
            if good:
                final = os.path.join(OUT_DIR, f"model_scan_{cid:02d}.onnx")
                os.replace(path, final)
                log(f"[OK] {final}  长度={cand}")
                for line in out.strip().splitlines():
                    log("      " + line)
                saved.append(final)
                ok = True
                break
            os.remove(path)
        if not ok:
            log(f"[!] id {cid} 无法裁剪成合法 ONNX（可能不是完整模型块）")

    if saved:
        log(f"[DONE] 成功 dump {len(saved)} 个模型")
        return 0
    log("[MISS] 未得到合法 ONNX")
    return 1


if __name__ == "__main__":
    sys.exit(main())
