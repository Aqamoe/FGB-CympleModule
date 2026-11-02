# 更新日志

遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/) 与语义化版本。

## [未发布]

## [0.1.0] - 未发布
### 新增
- Cymple VRCFT 兼容 UDP（292 字节 / 22999 端口）→ 注视方向、睁眼度、瞳孔扩张度与
  Unified Expressions 表情（70 字段，眼域/嘴域按包内 Flags 分域输出）。
- 严格线格式校验：魔数（小端）、Flags（大端）、msgType、payloadSize、非有限值整包拒收，
  Pitch/Yaw 裁剪 [-1,1]、其余裁剪 [0,1]。
- `config.ini` 支持覆盖端口。
- 协议单测 + 通过真实 ABI 的端到端冒烟测试。

### 已知限制
- 未在真机（Cymple 设备 + PC 端）验证，状态为未验证。
- 眼动单位按第三方核对为 [-1,1] 归一值（非「度」）；极化与瞳孔 0 的语义待真机确认。
- 不输出头姿、2D 注视点与毫米瞳孔直径——报文里没有或换算未确认。
