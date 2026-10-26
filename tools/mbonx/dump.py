#!/usr/bin/env python
"""mbonx 明文 dump 宿主脚本。

在样本进程内挂钩 ONNX Runtime 的 CreateSessionFromArray，把交给 ORT 的模型字节原样落盘。
产物一律写到 --out（默认 samples/mbonx/dumped），不污染源码树。

用法:
    python tools/mbonx/dump.py --exe "D:/MyProject/havoc_fake_server/Havoc529/BeeEdit.exe"
    python tools/mbonx/dump.py --pid 1234 --seconds 60
    python tools/mbonx/dump.py --exe ... --scan      # 只做内存扫描，不挂钩
"""
import argparse
import datetime as dt
import json
import os
import sys
import time

import frida

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
DEFAULT_OUT = os.path.join(ROOT, "samples", "mbonx", "dumped")
LOG_DIR = os.path.join(ROOT, "samples", "logs")


def now():
    return dt.datetime.now().strftime("%Y-%m-%d %H:%M:%S")


class Dumper:
    def __init__(self, out_dir):
        self.out_dir = out_dir
        os.makedirs(out_dir, exist_ok=True)
        os.makedirs(LOG_DIR, exist_ok=True)
        self.log_path = os.path.join(LOG_DIR, f"frida_{dt.datetime.now():%Y%m%d_%H%M%S}.log")
        self.saved = []

    def log(self, msg):
        line = f"[{now()}] {msg}"
        print(line, flush=True)
        with open(self.log_path, "a", encoding="utf-8") as f:
            f.write(line + "\n")

    def on_message(self, message, data):
        if message.get("type") == "error":
            self.log(f"[!] 脚本错误: {message.get('description')}")
            return
        payload = message.get("payload") or {}
        kind = payload.get("type")
        if kind == "log":
            self.log(payload.get("msg", ""))
        elif kind == "model" and data:
            seq = payload.get("seq", len(self.saved))
            length = payload.get("len", len(data))
            head = bytes(data[:8]).hex(" ")
            name = f"model_{seq:02d}_{length}.onnx"
            path = os.path.join(self.out_dir, name)
            with open(path, "wb") as f:
                f.write(data)
            self.saved.append(path)
            self.log(f"[OK] 已落盘 {name} ({length} 字节, head={head})")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", help="样本可执行文件路径（spawn 模式）")
    ap.add_argument("--pid", type=int, help="附加到已运行进程（attach 模式）")
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--seconds", type=int, default=90, help="运行/观察时长")
    ap.add_argument("--scan", action="store_true", help="只做内存扫描，不挂钩")
    ap.add_argument("--agent", default=os.path.join(HERE, "dump_ort.js"))
    args = ap.parse_args()

    if not args.exe and not args.pid:
        ap.error("需要 --exe 或 --pid")

    dumper = Dumper(args.out)
    with open(args.agent, encoding="utf-8") as f:
        js = f.read()

    device = frida.get_local_device()
    pid = None
    spawned = False
    if args.exe:
        exe = os.path.abspath(args.exe)
        if not os.path.isfile(exe):
            dumper.log(f"[!] 找不到可执行文件: {exe}")
            return 2
        dumper.log(f"[*] spawn: {exe}")
        pid = device.spawn([exe], cwd=os.path.dirname(exe))
        spawned = True
    else:
        pid = args.pid
        dumper.log(f"[*] attach pid={pid}")

    session = device.attach(pid)
    script = session.create_script(js)
    script.on("message", dumper.on_message)
    script.load()

    if spawned:
        device.resume(pid)
        dumper.log("[*] 已恢复进程，等待模型加载…")
    else:
        dumper.log("[*] 已注入，等待模型加载…")

    if args.scan:
        try:
            hits = script.exports_sync.scanonnx(4)
            dumper.log(f"[scan] 结果: {json.dumps(hits, ensure_ascii=False)}")
        except Exception as e:  # noqa: BLE001
            dumper.log(f"[!] 扫描失败: {e}")

    deadline = time.time() + args.seconds
    try:
        while time.time() < deadline:
            time.sleep(0.5)
            if dumper.saved:
                # 抓到就再等 3 秒收尾（可能有多模型）
                time.sleep(3)
                break
    except KeyboardInterrupt:
        dumper.log("[*] 手动中断")

    if spawned:
        try:
            device.kill(pid)
            dumper.log("[*] 已结束样本进程")
        except Exception:  # noqa: BLE001
            pass

    if dumper.saved:
        dumper.log(f"[DONE] 共落盘 {len(dumper.saved)} 个文件:")
        for p in dumper.saved:
            dumper.log("        " + p)
        return 0
    dumper.log("[MISS] 没有截获到模型（样本可能未走到推理初始化）")
    return 1


if __name__ == "__main__":
    sys.exit(main())
