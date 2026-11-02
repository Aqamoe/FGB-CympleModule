#pragma once
// Cymple（Project Cymple）VRCFT 兼容 UDP 包的线格式常量与构造工具。
//
// 线格式按接收端 Dominocs/CympleFaceTracking v1.5.2 的 TryParsePacket 逐项核对，
// 并用第三方 OpenCymple 的编解码交叉验证（详见 docs/protocol.md）。
// 本仓库是独立实现，未复制任何上游源码（发送端与接收端均为 GPL-3.0）。
//
// 292 字节固定包：
//   [0..3]   魔数 0xFFFFFFFD，uint32 小端落盘（FD FF FF FF）
//   [4..7]   Flags，uint32 **大端**落盘；bit0=有嘴部数据，bit1=有眼部数据
//   [8..9]   msgType，uint16 小端，恒为 0（BLENDSHAPEDATA）
//   [10..11] payloadSize，uint16 小端，恒为 280（70 * 4）
//   [12..]   70 个 float32 小端，字段顺序见 kFieldNames

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace cymple {

inline constexpr std::uint16_t kDefaultPort = 22999;
inline constexpr std::size_t kFieldCount = 70;
inline constexpr std::size_t kHeaderBytes = 12;
inline constexpr std::size_t kPayloadBytes = kFieldCount * sizeof(float);   // 280
inline constexpr std::size_t kPacketBytes = kHeaderBytes + kPayloadBytes;   // 292

inline constexpr std::uint32_t kMagic = 0xFFFFFFFDu;
inline constexpr std::uint16_t kMsgTypeBlendshapes = 0;

inline constexpr std::uint32_t kFlagMouth = 0x1u;
inline constexpr std::uint32_t kFlagEye = 0x2u;
inline constexpr std::uint32_t kFlagKnown = kFlagMouth | kFlagEye;

// 域划分与上游一致：前 20 个字段属于眼域，后 50 个属于嘴域。
inline constexpr std::size_t kEyeFieldCount = 20;
// 前 4 个字段（Pitch_L/Pitch_R/Yaw_L/Yaw_R）是 [-1,1] 归一值，其余是 [0,1]。
inline constexpr std::size_t kSignedFieldCount = 4;

inline constexpr std::size_t kFieldEyePitchLeft = 0;
inline constexpr std::size_t kFieldEyePitchRight = 1;
inline constexpr std::size_t kFieldEyeYawLeft = 2;
inline constexpr std::size_t kFieldEyeYawRight = 3;
inline constexpr std::size_t kFieldPupilLeft = 4;
inline constexpr std::size_t kFieldPupilRight = 5;
inline constexpr std::size_t kFieldLidCloseLeft = 6;
inline constexpr std::size_t kFieldLidCloseRight = 7;
inline constexpr std::size_t kFieldSquintLeft = 8;
inline constexpr std::size_t kFieldSquintRight = 9;
inline constexpr std::size_t kFieldWideLeft = 10;
inline constexpr std::size_t kFieldWideRight = 11;
inline constexpr std::size_t kFieldNoseSneerLeft = 20;
inline constexpr std::size_t kFieldJawOpen = 31;
inline constexpr std::size_t kFieldMouthSmileLeft = 51;
inline constexpr std::size_t kFieldTongueOut = 61;

// 70 个字段的官方命名（与上游 blendShapeNames 一致），只用于日志与测试断言。
inline constexpr std::array<const char*, kFieldCount> kFieldNames = {
    "EyePitch_L",     "EyePitch_R",        "EyeYaw_L",          "EyeYaw_R",
    "Eye_Pupil_Left", "Eye_Pupil_Right",   "EyeLidCloseLeft",   "EyeLidCloseRight",
    "EyeSquintLeft",  "EyeSquintRight",    "EyeWideLeft",       "EyeWideRight",
    "BrowLowererLeft", "BrowLowererRight", "BrowInnerUpLeft",   "BrowInnerUpRight",
    "BrowOuterUpLeft", "BrowOuterUpRight", "BrowPinchLeft",     "BrowPinchRight",
    "NoseSneerLeft",  "NoseSneerRight",    "CheekSquintLeft",   "CheekSquintRight",
    "CheekPuffLeft",  "CheekPuffRight",    "CheekSuckLeft",     "CheekSuckRight",
    "JawLeft",        "JawRight",          "JawFwd",            "JawOpen",
    "MouthClosed",    "MouthUpperShiftLeft", "MouthUpperShiftRight",
    "MouthLowerShiftLeft", "MouthLowerShiftRight",
    "LipSuckUL",      "LipSuckUR",         "LipSuckDL",         "LipSuckDR",
    "MouthFunnelUL",  "MouthFunnelUR",     "MouthFunnelDL",     "MouthFunnelDR",
    "MouthPuckerUpper", "MouthPuckerLower", "LipRaiseUL",       "LipRaiseUR",
    "LipDepressDL",   "LipDepressDR",      "MouthSmileLeft",    "MouthSmileRight",
    "MouthDimpleLeft", "MouthDimpleRight", "MouthFrownLeft",    "MouthFrownRight",
    "MouthStretchLeft", "MouthStretchRight", "MouthShrugUpper", "MouthShrugLower",
    "TongueOut",      "TongueLeft",        "TongueRight",       "TongueUp",
    "TongueDown",     "TongueCurlUp",      "TongueBendDown",    "TongueFlat",
    "TongueRoll",
};

namespace detail {

inline void WriteU16Le(std::uint8_t* p, std::uint16_t v) {
    p[0] = static_cast<std::uint8_t>(v & 0xFFu);
    p[1] = static_cast<std::uint8_t>(v >> 8);
}

} // namespace detail

// 把 70 个 float 按小端写进缓冲区（下标 i 对应字段 kFieldNames[i]）。
inline void PutFloat(std::uint8_t* bytes, std::size_t index, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    std::uint8_t* p = bytes + index * sizeof(std::uint32_t);
    p[0] = static_cast<std::uint8_t>(bits & 0xFFu);
    p[1] = static_cast<std::uint8_t>((bits >> 8) & 0xFFu);
    p[2] = static_cast<std::uint8_t>((bits >> 16) & 0xFFu);
    p[3] = static_cast<std::uint8_t>(bits >> 24);
}

// 测试/冒烟用的合法包构造器：头 12 字节 + 70 个小端 float。
inline void BuildPacket(std::uint8_t (&out)[kPacketBytes], std::uint32_t flags,
                        const std::array<float, kFieldCount>& values) {
    std::memset(out, 0, sizeof(out));
    const std::uint32_t magic = kMagic;
    out[0] = static_cast<std::uint8_t>(magic & 0xFFu);        // FD FF FF FF
    out[1] = static_cast<std::uint8_t>((magic >> 8) & 0xFFu);
    out[2] = static_cast<std::uint8_t>((magic >> 16) & 0xFFu);
    out[3] = static_cast<std::uint8_t>(magic >> 24);
    out[4] = static_cast<std::uint8_t>(flags >> 24);          // Flags 大端
    out[5] = static_cast<std::uint8_t>((flags >> 16) & 0xFFu);
    out[6] = static_cast<std::uint8_t>((flags >> 8) & 0xFFu);
    out[7] = static_cast<std::uint8_t>(flags & 0xFFu);
    detail::WriteU16Le(out + 8, kMsgTypeBlendshapes);
    detail::WriteU16Le(out + 10, static_cast<std::uint16_t>(kPayloadBytes));
    for (std::size_t i = 0; i < kFieldCount; ++i) {
        PutFloat(out + kHeaderBytes, i, values[i]);
    }
}

} // namespace cymple
