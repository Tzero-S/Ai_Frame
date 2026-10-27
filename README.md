# AiFrame — 实时视觉推理工程框架

> **本项目仅供学习与研究参考。** 请在遵守所在地法律法规、以及所使用软件/平台服务条款的前提下，
> 仅将其用于自己拥有或已获授权的环境中做技术验证。作者不对任何不当使用承担责任。

一个以「低延迟视觉推理」为主线的工程实践项目：把 **屏幕采集 → 模型推理 → 几何换算 → 控制信号输出**
串成一条可观测、可测试、可替换的管线，并提供一套桌面控制台做参数下发与状态观测。

代码按商业软件的工程规范组织（分层、依赖倒置、契约测试、CI），
适合作为 C++20 / ONNX Runtime / Electron 的综合练习与参考实现。

---

## 目录

- [1. 项目简介](#1-项目简介)
- [2. 技术架构](#2-技术架构)
- [3. 功能清单](#3-功能清单)
- [4. 环境要求](#4-环境要求)
- [5. 构建](#5-构建)
- [6. 运行](#6-运行)
- [7. 控制台](#7-控制台)
- [8. 测试与自检](#8-测试与自检)
- [9. 注意事项](#9-注意事项)
- [10. 常见问题](#10-常见问题)
- [11. 目录结构](#11-目录结构)
- [12. 仓库范围](#12-仓库范围)
- [13. 声明](#13-声明)

---

## 1. 项目简介

实时视觉系统里最难的不是"跑通一次推理"，而是**把整条链路的延迟、精度、可观测性同时管住**。
这个项目围绕这件事做了一次完整的工程化尝试：

| 关注点 | 做法 |
| --- | --- |
| 低延迟 | 零拷贝采集、双缓冲「保持最新帧」、采集与推理解耦、子步进平滑 |
| 精度 | ROI 感知坐标反投影、逐帧增量处理、按真实分辨率做几何换算 |
| 可观测 | 状态快照、分段健康度、逐帧耗时拆解、结构化日志 |
| 可测试 | 硬件相关段全部经 SPI 解耦，Fake 实现可跑完整链路；硬件用例可跳过 |
| 并发安全 | 跨线程共享的状态一律加锁；IPC 每连接独立线程，句柄由持有线程关闭 |
| 输入边界 | JSON 解析限深、配置 50 条校验、模型解码前校验张量形状、外部命令参数白名单 |
| 可替换 | 采集 / 推理 / 输出 / 覆盖层四类后端均可插拔，注册名与接口稳定 |
| 可配置 | JSON 配置 + 38 条校验 + 原子落盘 + 版本迁移，控制台可视化下发 |

**技术栈**：C++20 / CMake / ONNX Runtime 1.20（CPU·DML）/ Win32（DXGI、GDI、分层窗口）/
Electron 32 + React 18 + TypeScript + Ant Design 5。

---

## 2. 技术架构

```
                 ┌──────────── 控制台（桌面端）────────────┐
                 │ 参数下发 · 状态观测 · 模型管理 · 诊断    │
                 └───────┬──────────────────────▲─────────┘
            config.apply │                      │ status.snapshot
                         ▼                      │
  ┌────────────────────────────────────────────────────────────┐
  │ 引擎（C++20）                                               │
  │                                                            │
  │  采集 ──► 推理 ──► 后处理 ──► 几何/轨迹 ──► 输出            │
  │  DXGI/GDI    ORT        NMS/类别     坐标映射      多通道    │
  │  投屏适配   CPU/DML     坐标反投影   平滑/预测     可插拔    │
  │     │                      │            │                  │
  │     └─ 双缓冲              └─ 可视化     └─ 曲线参数         │
  └────────────────────────────────────────────────────────────┘
```

- **通信**：长度前缀 JSON over Win32 命名管道；管道名每次启动随机生成，通过临时发现文件握手。
- **配置**：schema v4，写入采用「临时文件 → 原子替换」，校验失败整份拒绝，不会留下半份配置。
- **状态**：`status.snapshot` 推送帧率、分段健康度、逐帧耗时与当前检测结果，控制台周期拉取。

---

## 3. 功能清单

### 3.1 已实现

**采集层**

- DXGI Desktop Duplication（D3D11），支持 ROI 裁剪、整块输出、丢失自动重建
- GDI BitBlt 降级路径
- 移动端投屏适配（adb 截图 + 内存流解码）
- 异步采集泵 + 双缓冲，背压策略为「保持最新帧」，`submitted = acquired + dropped` 可校验

**推理层**

- ONNX Runtime 1.20 后端，CPU 可用，DML 由编译开关控制
- YOLO 多代输出布局解码（v5/v6/v7、v8/v9/v10/v11、分割、姿态、端到端）
- 类别名从模型 `metadata_props["names"]` 自动读取，不再写死类别表
- 模型校验：I/O 形状、动态轴、类别元数据、特征维度一致性、SHA-256 指纹
- 模型预览：复用同一后端与后处理，把检测结果回传控制台可视化
- SSE2 预处理加速（可 A/B 关闭）

**后处理与几何**

- 逐类 NMS、类别白名单（按名字匹配，兼容大小写与缺省）
- ROI 感知的坐标反投影，正确处理裁剪区域与缩放
- 分辨率识别：目标屏 → 主屏 → 兜底三级，逐级记录日志；运行中尺寸变化按真实宽高比重算几何参数
- 视场换算：按宽高比查表 + 手动覆盖优先

**轨迹与控制信号**

- 目标点选择、范围约束（矩形 / 圆形 / 角度）、切换迟滞
- 临界阻尼平滑（含子步进与加加速度限幅），可选指数平滑
- 短时运动预测与延迟补偿
- 参数化的连续输入调节模块（可选，参数范围可由使用者调整）
- 输出通道抽象：系统级输入事件、网络桥接、串口桥接，可插拔切换

**可视化**

- 分层透明置顶窗口、点击穿透、软件光栅化、预乘 alpha
- 实时预览模式，可在不产生真实输入的情况下观察检测效果

**控制面与外壳**

- 配置中心：schema v4、38 条校验、原子落盘、v3→v4 迁移
- IPC 命令：`engine.hello` / `config.get` / `config.apply` / `status.snapshot` /
  `model.import` / `model.preview` / `weapon.patterns` / `fov.estimate`
- 控制台 10 个页面（白色主题，三栏布局）
- 引擎进程托管：探测已有实例优先复用，否则拉起；只管理自己启动的进程
- 桌面外壳：单实例、托盘、窗口几何记忆、便携打包

**隐私与稳健性**

- 日志路径脱敏（绝对路径压成文件名），单点生效
- 窗口类名与标题中性化，不占用任务栏与 Alt-Tab
- 可选的节奏控制与行为整形（默认关闭）
- 可选的调试器检测（默认关闭）

**控制面加固**

- 管道 DACL 只授权对象所有者（当前登录用户），其他账户连不上
- 每个连接独立工作线程（上限 8），不再串行阻塞第二个客户端
- 请求超过 16 MB 返回明确的 `413` 错误帧，而不是静默断连
- 发现文件同样收紧 ACL，避免被同机其他账户改写指向
- 子进程只继承必要的两个句柄（不泄漏调用方的 stdout 管道）

### 3.2 占位实现（接口已就绪）

`capture_stub` / `backend_stub` / `actuator_stub` / `overlay_stub`：
无硬件环境下的降级实现，接口与注册名稳定，替换实现即可接入。**请勿当作可用功能。**

### 3.3 未开发 / 规划中

| 项 | 现状 |
| --- | --- |
| 移动端投屏的 H.264 直解 | 未做，目前走截图降级路径，延迟偏高 |
| DML 后端在 MinGW 下构建 | 不支持，需 MSVC + Windows SDK |
| 外部硬件桥接的端到端验证 | 协议层已验证，整机联调待补 |
| 按软件分组的参数预设 | 未做，计划以 JSON 预设下发 |
| 多模型热切换 | 未做，换模型需重启引擎 |
| 姿态 / 关键点驱动 | 解码器已支持，上层未接 |
| 距离与弹道推算 | 未做 |
| INT8 量化提速 | 体积显著下降，但本机 CPU 未观察到提速，精度未充分验证 |
| 模型权重 | **不提供**，需自备 ONNX 模型 |
| 安装器 / 代码签名 / 自动更新 | 未做，便携 zip 分发 |
| 运行期显示模式变更监听 | 未做，靠「帧尺寸优先」在下一帧自愈 |

---

### 3.4 最近一轮质量加固

| 类别 | 修复内容 |
| --- | --- |
| 并发 | 运行时状态（帧计数 / 耗时 / 检测列表）跨线程访问统一加锁，消除 IPC 线程与推理线程的竞争 |
| 越界 | 模型输出解码前校验特征维与类别数匹配，形状不一致直接返回错误而不是越界读 |
| 外部命令 | 投屏采集不再经 shell 拼接，改为直接启动 `adb.exe` + 参数白名单 + 独立 stdout 管道 |
| 解析健壮性 | JSON 解析限深 128（20 KB 深嵌套原会栈溢出）、严格数字文法、UTF-16 代理对正确合并 |
| 生命周期 | 管道关闭改为「取消 I/O → 等工作线程返回 → 由持有者关闭句柄」；退出路径统一走 RAII 清理 |
| 配置 | 校验规则从 38 条扩到 50 条（目标屏序号 / 输出通道参数 / 容器规模上限 / 按键名长度），配置文件大小上限 4 MB |
| 语义修正 | `--serve` 现在读取 `--config` 并在 `config.apply` 后落盘；状态快照如实报告「未运行数据面」 |
| 体验 | 未识别的配置字段会打日志（拼错不再静默）、控制台轮询加在途保护、大响应收包改增量累积 |

---

## 4. 环境要求

| 组件 | 版本 | 说明 |
| --- | --- | --- |
| Windows | 10 1809+ / 11 x64 | 采集与分层窗口依赖 |
| CMake | 3.20+ | 引擎构建 |
| 编译器 | MSVC 2022（推荐）或 MinGW-w64 g++ 13+ | DML 后端仅 MSVC |
| ONNX Runtime | 1.20.x | 可选，关闭后仅编译接口与占位后端 |
| Node.js | 18+ | 控制台 |
| Python | 3.10+ | 可选，辅助脚本 |

> 需要**真实的交互式桌面会话**。无头会话 / Session 0 下取不到画面，测试请设 `AIM_SKIP_HW_TESTS=1`。

---

## 5. 构建

### 5.1 引擎

```bash
# 有 ONNX Runtime（推荐）
cmake -S engine -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DORT_DIR=/path/to/onnxruntime
cmake --build build --parallel

# 没有 ORT：只编译接口与占位后端
cmake -S engine -B build -G "Ninja" -DAIM_WITH_ORT=OFF
cmake --build build --parallel
```

产物：`build/src/helper.exe`、`build/tests/*_test.exe`。

| 选项 | 默认 | 说明 |
| --- | --- | --- |
| `AIM_WITH_ORT` | ON | 关闭后编入占位后端，加载时给出明确原因 |
| `ORT_DIR` | `$ENV{ORT_DIR}` | ORT 根目录；找不到会自动降级并提示 |
| `AIM_ENABLE_DML` | ON | 需要 MSVC，非 MSVC 自动关闭 |

> 记得把 ORT 的 `onnxruntime.dll` 放到 `helper.exe` 同目录，详见 [§10](#10-常见问题)。

### 5.2 控制台

```bash
cd console
npm install
npm run build       # 类型检查 + 构建
npm run electron    # 启动桌面端
```

开发模式：`npm run dev`（热更新）+ 另一个终端 `npm run electron`。

### 5.3 打包（可选）

```bash
cd console && npm run build && npm run package
# → dist-pkg/AiFrame/ 与 AiFrame-<ver>-win-x64.zip
```

---

## 6. 运行

```bash
# 只观察、不产生输入（推荐先跑这个确认链路）
helper.exe --preview --model model.onnx --backend cpu --frames 300

# 闭环运行（会产生真实输入事件，请在隔离环境中验证）
helper.exe --run --model model.onnx --config my-config.json

# 仅启动 IPC 服务端，由控制台下发参数
helper.exe --serve
```

补充说明：

- `--serve` 只提供控制面，不跑数据面，因此 `status.snapshot` 会如实返回 `running=false`。
- `--serve --config x.json` 会加载该文件；之后每次 `config.apply` 成功后自动写回同一路径。
- `--preview --config x.json` 会采用其中的 `capture.output_monitor`（其余参数预览固定用模型输入尺寸）。

| 参数 | 说明 |
| --- | --- |
| `--selfcheck` | 校验默认配置与内置注册项 |
| `--validate-config PATH` | 校验配置文件 |
| `--dump-default-config PATH` | 导出默认配置 |
| `--probe-model PATH` | 检查模型结构、类别与指纹 |
| `--serve [--pipe NAME]` | 仅启动命名管道服务端 |
| `--run [--config P] [--model P] [--frames N] [--dry-run] [--no-overlay] [--no-pipe]` | 闭环运行 |
| `--preview [--model P] [--backend cpu\|dml] [--capture dxgi\|gdi] [--frames N] [--no-show] [--roi N]` | 实时预览 |
| `--threads N` | 覆盖推理线程数（0 = 自动） |
| `--version` / `--help` | 版本 / 帮助 |

配置示例（节选，完整字段可用 `--dump-default-config` 导出）：

```jsonc
{
  "schema_version": 4,
  "capture": { "mode": "dxgi", "output_monitor": 0,
               "roi": { "mode": "center_square", "edge": 960 } },
  "inference": { "backend": "cpu", "imgsz": 960, "threads": 4,
                 "target_fps": 120, "async_capture": true,
                 "conf_threshold": 0.35, "iou_threshold": 0.45,
                 "selected_class_names": ["Body", "Head"] },
  "aim": { "target_point": { "preset": "head", "offset_y_ratio": 0.12 },
           "scope": { "mode": "rect", "half_w": 320, "half_h": 240 },
           "smoothing": { "enabled": true, "model": "critical", "omega": 30 } },
  "fov": { "auto": true, "manual_override": false, "hfov_deg": 103, "ads_zoom": 1 },
  "actuator": { "channel": "sendinput" }
}
```

> 上述示例可直接被 `helper.exe --validate-config` 接受（本机验证：`config ok: schema v4`）。

---

## 7. 控制台

10 个页面，白色主题，三栏布局：左侧导航、中间参数、右侧运行状态。

准星与瞄准 · 视场与分辨率 · 采集与投屏 · 快捷键绑定 · 连续输入调节 · 输出通道 ·
轨迹曲线 · 模型管理 · 稳健性与隐私 · 状态与诊断

特点：

- **单一真源**：分辨率、类别名、曲线库、视场查表都以引擎为准，界面只显示与下发，不重复实现。
- **离线可用**：引擎未启动时自动退化为 mock，界面不会白屏。
- **进程可见**：诊断页可查看引擎状态、PID、管道名，并可启动 / 停止 / 重启。
- **变更可见**：有未下发修改时顶栏提示，应用后显示版本号。

---

## 8. 测试与自检

```bash
cd build && ctest --output-on-failure          # 引擎：42 个测试
cd engine && bash tests/run_selfcheck.sh       # 引擎：严格模式自检（零警告 + 全量断言）
cd console && npm run typecheck && npm test && npm run e2e
```

当前状态（Windows 11 / MinGW g++ 16.2 / ORT 1.20.1 CPU，2560×1600@125%）：

| 项 | 结果 |
| --- | --- |
| `ctest` | **42/42 通过**（约 28 s） |
| `run_selfcheck.sh` | 全 PASS（43 个 PASS，含二进制自检，exit 0） |
| 断言总数 | `fov` 235 / `pipeline` 73 / `runtime` 43 / `postprocess` 63 / `hardening` 96 / `spi_ipc` 66 / `core` 94 / `scrcpy` 59 |
| 控制台 | typecheck / build / test(10) / e2e 全通过 |

CI 三个任务（`.github/workflows/ci.yml`，均为 windows-latest）：

| 任务 | 内容 |
| --- | --- |
| `build-test` | MSVC 构建（关闭 ORT）+ CTest + 二进制自检；`AIM_SKIP_HW_TESTS=1` 跳过硬件用例 |
| `asan` | 同套用例在 ASan 下再跑一遍，用于抓并发/越界类问题（当前为**建议性**任务，暂不阻断合并） |
| `console` | 控制台 typecheck / build / IPC 冒烟 |

> 第三方 action 已固定到 commit SHA，并显式声明 `permissions: contents: read`。

> 硬件相关用例（DXGI / GDI / 投屏）依赖真实桌面会话，与并行程序争用采集时可能偶发失败；
> 需要确定性结果时统一加 `AIM_SKIP_HW_TESTS=1`。

---

## 9. 注意事项

1. **仅供学习与研究**。请遵守所在地法律法规与所用软件 / 平台的服务条款；仅在自有或已授权环境中验证。
   本项目不鼓励、也不为任何违规用途提供支持。
2. **不提供模型与素材**。`samples/` 已被忽略，模型需自备；仓库内的曲线文件仅为形状样例。
3. **需要真实桌面会话**。锁屏、远程会话断开、无头环境都会导致采集失败。
4. **性能参考**：960×960 输入在 32 核 CPU 上约 14–15 ms/帧。追求更高帧率请启用 GPU 后端或降低输入尺寸。
5. **参数比建模更重要**。本项目刻意不做「按场景自动换算」，而是把可调参数范围放大、交给使用者判断，
   避免依赖不可靠的先验假设。
6. **仓库暂无 `LICENSE`**，默认保留所有权利。对外分发前请先补充许可证。

---

## 10. 常见问题

**Q1：`helper.exe --serve` 启动后立刻退出。**
进程用标准输入阻塞等待退出信号。若由父进程拉起且 stdin 指向空设备，会立刻读到 EOF 而退出。
拉起时请保持 stdin 打开（Node 示例：`stdio: ['pipe', 'ignore', 'ignore']`）。

**Q2：控制台提示「引擎启动超时」或连不上管道。**
管道名每次启动随机生成，控制台通过临时发现文件握手。残留的发现文件会指向旧管道；
若显式指定 `--pipe NAME`，引擎不写发现文件，此时需用 `AIM_PIPE` 环境变量告知控制台。

**Q3：加载模型时报符号缺失 / 直接崩溃。**
系统目录里可能已有旧版 `onnxruntime.dll` 被优先加载。请把 1.20.x 的 DLL 放到可执行文件同目录。

**Q4：MinGW 下编译 ORT 头文件报调用约定错误。**
追加 `-D_stdcall=__stdcall`。另外 DML 后端在 MinGW 下不可用，请改用 MSVC 或关闭该选项。

**Q5：CI 里硬件相关用例失败。**
设置 `AIM_SKIP_HW_TESTS=1`，相关用例会跳过而不是失败。

**Q6：`npm run electron` 报 `app is undefined`。**
环境中存在 `ELECTRON_RUN_AS_NODE=1`，Electron 退化为纯 Node。用 `npm run electron`（启动器会清理），
或手工 `env -u ELECTRON_RUN_AS_NODE -u NODE_OPTIONS <electron> <script>`。

**Q7：控制台显示的分辨率与系统设置不一致。**
历史问题已修复：`window.screen.width` 是 CSS 像素（125% 缩放下 1920×1080 会显示成 1536×864）。
现在统一走主进程读取的物理像素，与引擎同口径；若两者仍不同，说明控制台所在屏 ≠ 采集目标屏，界面会提示。

**Q8：换了模型后检测结果全被过滤。**
配置 v4 使用**类别名**而非索引。旧配置会自动迁移；若模型缺少类别元数据，则退化为允许全部类别。

**Q9：日志里的路径变成了文件名。**
路径脱敏默认开启。排查问题时把 `anti_detect.strip_paths` 设为 `false`。

**Q10：预览看不到可视化窗口。**
确认给了 `--model`、没有加 `--no-show`；独占全屏下覆盖层会被遮挡，请用无边框窗口模式。

**Q11：另一个账号 / 另一个进程连不上管道。**
这是设计如此：管道 DACL 只授权对象所有者（拉起引擎的那个用户），其他账户会被拒绝。
同机排查请用同一账号，或临时以该账号启动引擎。

**Q12：请求返回 `code=413 request too large`。**
单个请求上限 16 MB（`ipc/framing.h` 的 `kMaxFrameBytes`）。模型预览请把输入图缩到模型尺寸
（如 960×960），不要直接传整屏原图。

**Q13：`config.apply` 成功但某项没生效。**
字段名拼错时会被忽略并打日志：`config.apply: 忽略未知字段 aim.scope.half_widht`。
按日志里的路径改正即可；字段名以 `helper.exe --dump-default-config` 导出的为准。

**Q14：`--serve` 模式下控制台显示「未运行」。**
正常现象：`--serve` 只提供控制面、不跑数据面，`status.snapshot` 会如实返回 `running=false`。
需要真实运行时用 `--run`。

---

## 11. 目录结构

```
Ai_Frame/
├─ engine/                  C++20 引擎
│  ├─ src/
│  │  ├─ core/              配置 / 管线 / 运行时 / 双缓冲 / 日志
│  │  ├─ capture/           DXGI / GDI / 投屏适配 / ROI
│  │  ├─ inference/         ORT 后端 / 模型校验 / 预处理 / 预览
│  │  ├─ postprocess/       解码 / NMS / 类别过滤
│  │  ├─ aim/               几何换算 / 目标选择 / 平滑 / 预测
│  │  ├─ weapon/            曲线建模 / 参数库 / 连续输入调节
│  │  ├─ actuator/          输出通道（系统事件 / 网络 / 串口 / 曲线）
│  │  ├─ overlay/           分层窗口 / 光栅化 / 实时预览
│  │  ├─ fov/               视场换算 / 分辨率识别
│  │  ├─ ipc/               管道服务端 / 帧协议 / 命令处理
│  │  └─ opsec/             隐私与稳健性
│  ├─ data/patterns.json    曲线形状样例
│  └─ tests/                42 个测试套件 + run_selfcheck.sh
├─ console/                 Electron + React + TS 控制台
│  ├─ src/main/             窗口 / 托盘 / 进程托管 / 窗口状态
│  ├─ src/renderer/         10 个页面 + IPC 客户端 + mock
│  └─ scripts/              打包 / e2e / smoke / 启动器
├─ tools/                   曲线提取 · 量化 · 模型容器探针
├─ scripts/                 build.ps1 / ci_local.sh
└─ .github/workflows/ci.yml
```

---

## 12. 仓库范围

本仓库只包含**源码与本文档**：

| 包含 | 不包含 |
| --- | --- |
| `engine/` 引擎源码与测试 | 模型权重（需自备 ONNX 模型） |
| `console/` 控制台源码 | 录制素材与样本数据 |
| `tools/` `scripts/` `.github/` | 设计与方案类文档（单独维护） |

如需模型或扩展资料，请按 [§3.7](#37-模型) 的说明自备；`engine/data/patterns.json` 只是形状样例。

## 13. 声明

本项目为个人学习与工程实践产物，**仅用于技术研究与教学参考**。

- 请勿用于违反所在地法律法规、软件许可协议或平台服务条款的场景。
- 请在自有设备或已获明确授权的环境中进行验证。
- 使用本项目产生的任何后果由使用者自行承担。

仓库暂未指定开源许可证；在补充 `LICENSE` 之前，默认保留所有权利。
