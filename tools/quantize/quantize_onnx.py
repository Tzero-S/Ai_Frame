#!/usr/bin/env python
"""ONNX INT8 量化（QDQ 静态量化）。

为什么用 QDQ 静态量化：动态量化会把 Conv 变成 ConvInteger，而 ONNX Runtime 的
CPU EP 不实现该算子（NOT_IMPLEMENTED）。QDQ 走的是 QLinearConv 路径，CPU 可用，
也是 ORT 官方推荐的生产格式。

校准数据：
  --calib DIR   用目录里的图片（png/jpg）缩放成模型输入尺寸做校准（推荐）
  缺省          用随机张量校准——只能验证"能跑通"，精度不可信，会在输出里提示

用法:
    python tools/quantize/quantize_onnx.py --model in.onnx --out int8.onnx --calib ./imgs
    python tools/quantize/quantize_onnx.py --model in.onnx --out int8.onnx --check
"""
import argparse
import glob
import os
import sys
import time

import numpy as np


def human(n):
    return f"{n / 1048576:.2f} MiB"


class CalibReader:
    """把图片（或随机张量）喂给量化器。返回 dict: 输入名 → NCHW float32。"""

    def __init__(self, files, input_name, shape, count):
        self.files = files
        self.input_name = input_name
        self.shape = shape
        self.count = count

    def get_next(self):
        if self.count <= 0:
            return None
        self.count -= 1
        if self.files:
            from PIL import Image
            path = self.files[len(self.files) - self.count - 1] if self.count >= 0 else self.files[0]
            h, w = self.shape[2], self.shape[3]
            with Image.open(path) as im:
                arr = np.asarray(im.convert("RGB").resize((w, h)), dtype=np.float32) / 255.0
            chw = np.transpose(arr, (2, 0, 1))[None, ...]
            return {self.input_name: chw.astype(np.float32)}
        return {self.input_name: np.random.rand(*self.shape).astype(np.float32)}

    def rewind(self):
        pass


def main():
    ap = argparse.ArgumentParser(description="ONNX INT8 量化（QDQ 静态）")
    ap.add_argument("--model", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--calib", default="", help="校准图片目录（留空=随机张量，精度不可信）")
    ap.add_argument("--samples", type=int, default=32, help="校准样本数")
    ap.add_argument("--check", action="store_true", help="量化后用 ORT 跑一次推理自检")
    args = ap.parse_args()

    if not os.path.isfile(args.model):
        print(f"错误: 模型不存在 {args.model}")
        return 2

    try:
        import onnxruntime as ort
        from onnxruntime.quantization import (CalibrationDataReader, CalibrationMethod,
                                              QuantFormat, QuantType, quantize_static)
    except ImportError:
        print("错误: 需要 onnxruntime.quantization")
        return 2

    class Reader(CalibrationDataReader):
        def __init__(self, inner):
            self.inner = inner

        def get_next(self):
            return self.inner.get_next()

        def rewind(self):
            self.inner.rewind()

    # 读输入形状
    try:
        sess = ort.InferenceSession(args.model, providers=["CPUExecutionProvider"])
    except Exception as e:  # noqa: BLE001
        print(f"错误: 无法加载模型: {e}")
        return 2
    inp = sess.get_inputs()[0]
    shape = [d if isinstance(d, int) and d > 0 else 1 for d in inp.shape]
    if len(shape) != 4:
        print(f"错误: 仅支持 4 维输入，实际 {shape}")
        return 2

    files = []
    if args.calib:
        for ext in ("*.png", "*.jpg", "*.jpeg", "*.bmp"):
            files.extend(sorted(glob.glob(os.path.join(args.calib, ext))))
        if not files:
            print(f"错误: 校准目录里没有图片: {args.calib}")
            return 2
        print(f"校准数据: {len(files)} 张图片")
    else:
        print("⚠️ 未提供 --calib，使用随机张量校准：仅验证链路可用，精度不可信")

    src_size = os.path.getsize(args.model)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)) or ".", exist_ok=True)

    t0 = time.time()
    last_err = None
    # per-channel 会在 opset<13 的模型上产生 "Unrecognized attribute: axis"
    # （DequantizeLinear 直到 opset 13 才支持 axis），失败即回退逐张量量化。
    for per_channel in (True, False):
        reader = Reader(CalibReader(files, inp.name, shape, max(1, args.samples)))
        try:
            quantize_static(
                model_input=args.model,
                model_output=args.out,
                calibration_data_reader=reader,
                quant_format=QuantFormat.QDQ,
                per_channel=per_channel,
                activation_type=QuantType.QUInt8,
                weight_type=QuantType.QInt8,
                calibrate_method=CalibrationMethod.MinMax,
            )
            # 量化可能"成功"但产出非法图（opset<13 时 per-channel 会写错属性），
            # 必须实际加载一次才能确认。
            ort.InferenceSession(args.out, providers=["CPUExecutionProvider"])
            if not per_channel:
                print("  注：per-channel 不可用（模型 opset < 13），已回退逐张量量化")
            break
        except Exception as e:  # noqa: BLE001
            last_err = e
            if not per_channel:
                print(f"错误: 量化失败: {e}")
                return 2
    dt = time.time() - t0

    if not os.path.isfile(args.out):
        print("错误: 量化未产出文件")
        return 2
    dst_size = os.path.getsize(args.out)
    print(f"量化完成 {dt:.1f}s")
    print(f"  {human(src_size)} → {human(dst_size)}  "
          f"（{dst_size / src_size * 100:.1f}%，体积变化 {(1 - dst_size / src_size) * 100:+.1f}%）")

    if args.check:
        try:
            s2 = ort.InferenceSession(args.out, providers=["CPUExecutionProvider"])
            data = np.random.rand(*shape).astype(np.float32)
            t1 = time.time()
            out = s2.run(None, {inp.name: data})
            print(f"  自检推理 OK：{(time.time() - t1) * 1000:.0f} ms，"
                  f"输出 {out[0].shape} dtype={out[0].dtype}")
        except Exception as e:  # noqa: BLE001
            print(f"错误: 量化模型推理失败: {e}")
            return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
