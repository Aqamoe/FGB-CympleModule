# FGB CympleModule

[![build](https://github.com/Aqamoe/FGB-CympleModule/actions/workflows/ci.yml/badge.svg)](https://github.com/Aqamoe/FGB-CympleModule/actions/workflows/ci.yml)

**状态：0.1.0，未验证（untested）**。协议解析、映射与 ABI 生命周期有单元测试和合成 UDP
端到端冒烟；真机（Cymple 设备 + PC 端）验收待补，见 [docs/verification.md](docs/verification.md)。

[FaceGaze Bridge](https://github.com/Aqamoe/FaceGaze-Bridge) 的 Cymple 原生输入模块。
直接接收 **[Project Cymple](https://github.com/Dominocs/Project_Cymple)** PC 端发到本机
22999 端口的 292 字节二进制包（VRCFT 兼容格式），输出注视方向、睁眼度、瞳孔扩张度与
Unified Expressions 表情。不需要 VRCFT，不注入任何进程，也不需要管理员权限。

Native input module for FaceGaze Bridge. Reads the 292-byte binary datagrams your
Cymple device already sends and outputs per-eye gaze, eye openness, normalized pupil
dilation and Unified Expressions shapes. No VRCFT, no process injection, no admin rights.

## 功能 / Features

- **表情**：眼域 12 项 + 嘴域 56 项共 68 个 Unified Expressions 形状目标，逐项照抄上游已确认映射。
- **眼睛**：睁眼度 `1 - EyeLidClose`；注视方向由眼动 Pitch/Yaw（[-1,1] 归一值）转成
  HMD 局部单位向量，置信度 0.8；瞳孔写归一扩张度（0..1），不写没有依据的毫米直径。
- **按域输出**：包内 Flags 只点亮眼域或嘴域时，只有对应属性被写入，另一域不伪造数据。
- **可诊断**：端口冲突、包长/魔数/msgType/payloadSize 不符、非有限值都有明确日志，不静默丢数据。

## 不提供 / Not provided

| 数据 | 原因 |
|---|---|
| 头姿 | 报文里没有头姿字段 |
| 瞳孔毫米直径 | 上报值是 0..1 归一量纲，换算依据未确认 |
| 2D 注视点 / vergence | 报文没有对应字段 |
| 渲染级注视 | 需要 `gaze` 通道时请配真正的眼动注视源；本模块只填 `eyes` |

## 环境要求

| 项 | 要求 |
|---|---|
| 系统 | Windows 10 / 11 x64 |
| 宿主 | FaceGaze Bridge（原生模块 ABI v2） |
| 设备 | Cymple 面捕/眼动设备 + PC 端（线格式按 4.15.6 核对） |
| 其它 | 关闭 VRCFT 的 Cymple 模块：它和本模块抢同一个 UDP 端口 |

## 安装

1. 从 Releases 下载 `cymple-0.1.0.zip`。
2. FaceGaze Bridge →「模块库 → 原生模块 → 安装原生模块 (.zip)」，或「模块」页「从 .zip 安装」。
3. 「模块」页把 **Cymple Face Tracking** 设为启动，角色通常给 `expression` 与 `eyes`。
4. 重启引擎；Cymple 端开始发送后模块卡片应显示活跃。

## 配置

模块目录下可选 `config.ini`：

```ini
port = 22999
```

改完重启引擎，并在 Cymple 端同步改端口。未知键会被记录一条警告，不会静默失效。

## 从源码构建

需要 Visual Studio 2022（x64）、CMake ≥ 3.24 与 Node 20（脚本用）。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

SDK 与 Kit 由 `sdk.lock.json` / `kit.lock.json` 在配置阶段拉取并校验 SHA256，仓库里不含 SDK 头文件副本。
本地想指向已解压的 Kit：

```powershell
cmake -S . -B build -A x64 -DFGB_KIT_DIR="D:/code/FGB-Module/FGB-Module-Kit"
```

打包与测试：

```powershell
cmake --build build --config Release --target package-modules   # build/Release/cymple-<版本>.zip
ctest --test-dir build -C Release --output-on-failure           # 协议与映射单测
cmake --build build --config Release --target smoke-test        # 真实 ABI + 合成 UDP 冒烟（需 22999 空闲）
```

## 目录结构

```
src/main.cpp                ABI 生命周期、UDP 接收、状态上报
src/cymple_wire.hpp         线格式常量与包构造器（测试/冒烟共用）
src/cymple_payload.*        292 字节包解析与 Unified Expressions 写入
module/                     fgb-native.json 与图标
tests/                      协议单测、真实 ABI 冒烟
docs/protocol.md            线格式与映射依据（含已确认/未确认标注）
docs/verification.md        真机验收清单
cmake/                      依赖锁解析（SDK / Kit 都是拉取，不复制）
```

## 排错

| 现象 | 处理 |
|---|---|
| 卡片显示「无法绑定 UDP 22999，端口已被占用」 | 关闭 VRCFT 的 Cymple 模块或其它接收端；或换端口并同步改 Cymple 设置 |
| 一直提示「还没收到任何数据包」 | 确认 Cymple PC 端已开启对外输出、目标 IP 填的是本机地址、防火墙放行该端口 |
| 日志出现「丢弃一包：包长 xxx 字节」 | Cymple 版本或 VRChat 直连模式改变了输出，核对 [docs/protocol.md](docs/protocol.md) |
| 表情有、眼睛不动（或反之） | 看日志是否出现「Flags 未点亮眼/嘴域」；Cymple 端只开启了另一域 |
| 注视方向左右/上下相反 | 极化目前仅有第三方实现佐证、真机未验证；请在 issue 附设备与版本信息 |

## 致谢 / Attribution

- 线格式与映射依据参考 [CympleFaceTracking](https://github.com/Dominocs/CympleFaceTracking)
  （VRCFT 模块，GPL-3.0），并用 [OpenCymple](https://github.com/K1TTYBLACK/OpenCymple) 交叉核对；
  本仓库是该线格式的独立 C++ 实现，未包含其源码。
- 硬件与 PC 端：[Project Cymple](https://github.com/Dominocs/Project_Cymple)（GPL-3.0）。
- ABI 头与宿主：[FaceGaze Bridge](https://github.com/Aqamoe/FaceGaze-Bridge)；共享工具链：[FGB-Module-Kit](https://github.com/Aqamoe/FGB-Module-Kit)。

## 许可

Apache License 2.0，见 [LICENSE](LICENSE) 与 [NOTICE](NOTICE)。
