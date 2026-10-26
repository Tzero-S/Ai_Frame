"""生成 ORT 后端单测用的最小 ONNX 模型（可复现，不依赖训练框架）。

模型：input float32 [1,3,64,64] → 输出固定 YOLO-like 张量 [1,6,3]
  - ReduceMean(input) * 0 只用于让 input 参与计算（避免被优化掉）
  - base 常量即期望的检测输出（nc=2, 无 obj，F=4+2=6, A=3）
用法：python gen_test_model.py [输出路径]
"""
import sys
import numpy as np
import onnx
from onnx import helper, TensorProto

OUT = sys.argv[1] if len(sys.argv) > 1 else "test_yolo.onnx"

# [1, F=6, A=3]：cx,cy,w,h,cls0,cls1
base = np.array(
    [
        [32.0, 10.0, 0.5],   # cx
        [32.0, 10.0, 0.5],   # cy
        [10.0, 5.0, 0.2],    # w
        [20.0, 5.0, 0.4],    # h
        [0.2, 0.8, 0.1],     # cls0
        [0.9, 0.1, 0.1],     # cls1
    ],
    dtype=np.float32,
).reshape(1, 6, 3)

inp = helper.make_tensor_value_info("input", TensorProto.FLOAT, [1, 3, 64, 64])
out = helper.make_tensor_value_info("output", TensorProto.FLOAT, [1, 6, 3])

base_const = helper.make_node(
    "Constant", [], ["base"],
    value=helper.make_tensor("base_v", TensorProto.FLOAT, [1, 6, 3], base.flatten()),
)
zero_scalar = helper.make_node(
    "Constant", [], ["zero"],
    value=helper.make_tensor("zero_v", TensorProto.FLOAT, [], [0.0]),
)
reduce = helper.make_node("ReduceMean", ["input"], ["mean"], axes=[0, 1, 2, 3], keepdims=0)
mul = helper.make_node("Mul", ["mean", "zero"], ["z"])
add = helper.make_node("Add", ["base", "z"], ["output"])

graph = helper.make_graph(
    [base_const, zero_scalar, reduce, mul, add],
    "aim_test_yolo",
    [inp],
    [out],
)
model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 11)])
model.ir_version = 7
onnx.checker.check_model(model)
onnx.save(model, OUT)
print("wrote", OUT)
