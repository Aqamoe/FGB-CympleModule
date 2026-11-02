#pragma once
// Cymple 292 字节 UDP 包的解析与 FGB 帧写入。
//
// 解析严格对齐上游接收端的校验顺序：长度 → 魔数 → msgType → payloadSize →
// Flags（大端）→ 70 个小端 float。任一 float 非有限（NaN/±Inf）整包拒收；
// 前 4 个字段（Pitch/Yaw）裁剪到 [-1,1]，其余裁剪到 [0,1]。
// Flags 决定眼域/嘴域是否可信：对应位没点亮时，那一域的值不写入帧。

#include "cymple_wire.hpp"

#include "fgb/kit/host.hpp"
#include "fgb_module_api.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace cymple {

struct Packet {
    std::uint32_t flags = 0;
    bool eye_active = false;   // kFlagEye：前 20 个字段可信
    bool mouth_active = false; // kFlagMouth：后 50 个字段可信
    std::array<float, kFieldCount> values{};
};

// 解析一包。返回 false 表示整包不可信，调用方应丢弃并记日志。
bool ParsePacket(const std::uint8_t* data, std::size_t size, Packet& out, std::string& error);

// 登记本模块会写的全部 Unified Expressions 目标名。
void DeclareTargets(fgb::kit::ShapeTable& table);

// 按域标志写帧，返回实际置上的 attributes 位（0 表示这包没写任何东西）。
std::uint64_t WriteFrame(const Packet& packet, const fgb::kit::ShapeTable& table,
                         FgbFrameSlot* slot);

} // namespace cymple
