# Cymple VRCFT 兼容 UDP 协议要点

来源：Cymple PC 端（4.15.6 程序）向 `PC:22999` 发送的二进制数据报。
本模块按线格式独立实现：接收端 [Dominocs/CympleFaceTracking]（v1.5.2，GPL-3.0）与第三方
[OpenCymple] 的解析代码只作为核对依据，**未复制任何上游源码**。

标注约定：
- **已确认**：有可读的上游/第三方实现源码或回归夹具直接佐证；
- **未确认**：没有发送端源码可查（发送端 `cympleFaceMsg.h`/`face_osc.cpp` 未公开在 GitHub，
  OpenCymple 的检索也只命中接收侧），或语义存在多种解释，模块不据此伪造数据。

## 报文结构（292 字节固定长）

| 偏移 | 长度 | 字段 | 状态 |
|---|---|---|---|
| 0 | 4 | 魔数 `0xFFFFFFFD`，**小端**落盘（字节序 FD FF FF FF） | 已确认（接收端按 `ReadInt32LittleEndian` 校验） |
| 4 | 4 | Flags，**大端**落盘；bit0=`0x01` 有嘴部数据，bit1=`0x02` 有眼部数据 | 已确认（上游注释「only Flags is converted to big endian」+ 回归夹具按大端写） |
| 8 | 2 | msgType，小端 uint16，恒为 `0`（BLENDSHAPEDATA） | 已确认 |
| 10 | 2 | payloadSize，小端 uint16，恒为 `280`（=70×4） | 已确认 |
| 12 | 280 | 70 个 float32 **小端** | 已确认 |

- 包长必须**恰好** 292 字节：过短、超长都整包丢弃（接收端 `packet.Length != 292` 直接拒收）。
- Flags 其余位：上游只检查 bit0/bit1，其余位含义 **未确认**；本模块见到未知置位只记一条日志，不改变行为。
- 同一端口同一时刻只能有一个接收者（VRCFT 的 Cymple 模块会占 22999）。

## 70 个字段顺序（偏移 = 12 + index*4）

索引 0..19 属**眼域**（Flags bit1），20..69 属**嘴域**（Flags bit0）。顺序取自上游
`CympleFaceDataStructs`，且其回归夹具声明是「从发送端 C++ 头独立抄录」，与本表一致：

```text
0  EyePitch_L      1  EyePitch_R      2  EyeYaw_L       3  EyeYaw_R
4  Eye_Pupil_Left  5  Eye_Pupil_Right 6  EyeLidCloseLeft  7  EyeLidCloseRight
8  EyeSquintLeft   9  EyeSquintRight  10 EyeWideLeft     11 EyeWideRight
12 BrowLowererLeft 13 BrowLowererRight 14 BrowInnerUpLeft 15 BrowInnerUpRight
16 BrowOuterUpLeft 17 BrowOuterUpRight 18 BrowPinchLeft   19 BrowPinchRight
20 NoseSneerLeft   21 NoseSneerRight   22 CheekSquintLeft 23 CheekSquintRight
24 CheekPuffLeft   25 CheekPuffRight   26 CheekSuckLeft   27 CheekSuckRight
28 JawLeft         29 JawRight         30 JawFwd          31 JawOpen
32 MouthClosed     33 MouthUpperShiftLeft 34 MouthUpperShiftRight
35 MouthLowerShiftLeft 36 MouthLowerShiftRight
37 LipSuckUL       38 LipSuckUR        39 LipSuckDL       40 LipSuckDR
41 MouthFunnelUL   42 MouthFunnelUR    43 MouthFunnelDL   44 MouthFunnelDR
45 MouthPuckerUpper 46 MouthPuckerLower 47 LipRaiseUL      48 LipRaiseUR
49 LipDepressDL    50 LipDepressDR     51 MouthSmileLeft  52 MouthSmileRight
53 MouthDimpleLeft 54 MouthDimpleRight 55 MouthFrownLeft  56 MouthFrownRight
57 MouthStretchLeft 58 MouthStretchRight 59 MouthShrugUpper 60 MouthShrugLower
61 TongueOut       62 TongueLeft       63 TongueRight     64 TongueUp
65 TongueDown      66 TongueCurlUp     67 TongueBendDown  68 TongueFlat
69 TongueRoll
```

**已确认**：字段名与顺序（接收端结构体 + 索引写入函数 + 第三方解码器三方一致）。

## 数值范围与清洗

- 每个 float 必须有限：任一 NaN/±Inf 整包拒收（**已确认**，上游与回归夹具都这样做）。
- 索引 0..3（Pitch/Yaw）裁剪到 `[-1, 1]`；其余字段裁剪到 `[0, 1]`（**已确认**，上游 `Math.Clamp`）。
- **Pitch/Yaw 的单位：归一值 [-1,1]，不是度。** 证据：上游把这两个字段单独按 [-1,1] 钳位；
  第三方 OpenCymple 对同一字段做 `clamp(v, -1, 1)` 并注明「yaw > 0 = looking right,
  pitch > 0 = looking up」。**与「单位是度」的说法不符**（度不可能落在 ±1 内做钳位）。
  极化（正负号方向）来自第三方文档，**未做真机验证**。
  本模块把归一偏移按正切投影转成 HMD 局部单位向量（-Z 前），正 yaw → +x，正 pitch → +y。
- 睁眼度：`openness = 1 - EyeLidClose*`（**已确认**，上游逐字使用）。
- 瞳孔：范围 0..1 属**已确认**（上游注释「transmitted 0–1 range」，钳位也是 [0,1]）；
  0 是「最小/未扩张」还是「基线」**未确认**。因此只写 `pupil_dilation`（0..1 直接透传），
  **不写 `pupil_diameter_mm`**（毫米换算没有依据，写出来就是伪造）。

## 域与 Flags

- 上游按 Flags 分别维护「最新眼帧 / 最新嘴帧」：flags=2 的包只刷新眼域，flags=1 只刷新嘴域
  （**已确认**，回归测试 InterleavedFrames 场景）。
- 本模块同样只在对应域点亮时写对应属性；域未点亮时相关槽位保持 invalid，**不写 0**，
  以免「没上报」被当成「值就是 0」。
- 上游断流 1 秒即把对应域复位为中性值；本模块沿用 Kit 的 10 秒断流提示（保守，只报状态不伪造中性帧）。

## 表情映射

嘴域 50 个字段与眼域 12 个形状字段共映射到 68 个 Unified Expressions 目标；大部分为**同名直映**
（如 `JawOpen→JawOpen`），已确认的改名/一对多规则（照抄上游 Update 的写入关系）：

| 来源字段 | 目标形状 |
|---|---|
| `JawFwd` | `JawForward` |
| `MouthUpperShift*` / `MouthLowerShift*` | `MouthUpper*` / `MouthLower*` |
| `LipSuckUL/UR/DL/DR` | `LipSuckUpper*` / `LipSuckLower*` |
| `MouthFunnelUL/UR/DL/DR` | `LipFunnelUpper*` / `LipFunnelLower*` |
| `MouthPuckerUpper/Lower` | `LipPuckerUpper*`（左右同值）/ `LipPuckerLower*` |
| `LipRaiseUL/UR` | `MouthUpperUp*` + `MouthUpperDeepen*` |
| `LipDepressDL/DR` | `MouthLowerDown*` |
| `MouthSmile*` | `MouthCornerPull*` + `MouthCornerSlant*` |
| `MouthShrugUpper/Lower` | `MouthRaiserUpper` / `MouthRaiserLower` |

## 不提供 / 不伪造

| 数据 | 原因 |
|---|---|
| 头姿（head） | 报文里没有头姿字段 |
| 2D 注视点 / vergence | 报文里没有 |
| 瞳孔毫米直径 | 0..1 归一值到毫米的换算未确认 |
| 嘴部设备在离线状态 | 上游文档确认该格式没有嘴部新鲜度指示，断流只能靠包超时 |

[Dominocs/CympleFaceTracking]: https://github.com/Dominocs/CympleFaceTracking
[OpenCymple]: https://github.com/K1TTYBLACK/OpenCymple
[Cymple]: https://github.com/Dominocs/Project_Cymple
