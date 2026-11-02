#include "cymple_payload.hpp"

#include "fgb/kit/arkit.hpp"

#include <cmath>
#include <cstring>

namespace cymple {

namespace {

std::uint32_t ReadU32Le(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint32_t ReadU32Be(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

std::uint16_t ReadU16Le(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

float ReadFloatLe(const std::uint8_t* p) {
    const std::uint32_t bits = ReadU32Le(p);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// Pitch/Yaw 是 [-1,1] 归一值，其余字段是 [0,1] 强度。
float ClampField(std::size_t index, float value) {
    if (index < kSignedFieldCount) {
        if (value > 1.0f) {
            return 1.0f;
        }
        if (value < -1.0f) {
            return -1.0f;
        }
        return value;
    }
    return fgb::kit::Clamp01(value);
}

struct ShapeRule {
    std::size_t field;
    const char* target;
};

// 眼域（字段 0..19）里的表情形状；与上游 UpdateEyeData 的写入集合一致。
constexpr ShapeRule kEyeShapes[] = {
    {8, "EyeSquintLeft"},   {9, "EyeSquintRight"}, {10, "EyeWideLeft"},
    {11, "EyeWideRight"},   {12, "BrowLowererLeft"}, {13, "BrowLowererRight"},
    {14, "BrowInnerUpLeft"}, {15, "BrowInnerUpRight"}, {16, "BrowOuterUpLeft"},
    {17, "BrowOuterUpRight"}, {18, "BrowPinchLeft"},  {19, "BrowPinchRight"},
};

// 嘴域（字段 20..69）→ Unified Expressions。对应关系逐项照抄上游
// UpdateFacialExpressions 的「已确认」映射；一源多目标由 ShapeTable 的 max 合并。
constexpr ShapeRule kMouthShapes[] = {
    {20, "NoseSneerLeft"},   {21, "NoseSneerRight"},
    {22, "CheekSquintLeft"}, {23, "CheekSquintRight"},
    {24, "CheekPuffLeft"},   {25, "CheekPuffRight"},
    {26, "CheekSuckLeft"},   {27, "CheekSuckRight"},
    {28, "JawLeft"},         {29, "JawRight"},
    {30, "JawForward"},      {31, "JawOpen"},
    {32, "MouthClosed"},     {33, "MouthUpperLeft"},
    {34, "MouthUpperRight"}, {35, "MouthLowerLeft"},
    {36, "MouthLowerRight"}, {37, "LipSuckUpperLeft"},
    {38, "LipSuckUpperRight"}, {39, "LipSuckLowerLeft"},
    {40, "LipSuckLowerRight"}, {41, "LipFunnelUpperLeft"},
    {42, "LipFunnelUpperRight"}, {43, "LipFunnelLowerLeft"},
    {44, "LipFunnelLowerRight"}, {45, "LipPuckerUpperLeft"},
    {45, "LipPuckerUpperRight"}, {46, "LipPuckerLowerLeft"},
    {46, "LipPuckerLowerRight"}, {47, "MouthUpperUpLeft"},
    {47, "MouthUpperDeepenLeft"}, {48, "MouthUpperUpRight"},
    {48, "MouthUpperDeepenRight"}, {49, "MouthLowerDownLeft"},
    {50, "MouthLowerDownRight"}, {51, "MouthCornerPullLeft"},
    {51, "MouthCornerSlantLeft"}, {52, "MouthCornerPullRight"},
    {52, "MouthCornerSlantRight"}, {53, "MouthDimpleLeft"},
    {54, "MouthDimpleRight"}, {55, "MouthFrownLeft"},
    {56, "MouthFrownRight"}, {57, "MouthStretchLeft"},
    {58, "MouthStretchRight"}, {59, "MouthRaiserUpper"},
    {60, "MouthRaiserLower"}, {61, "TongueOut"},
    {62, "TongueLeft"}, {63, "TongueRight"}, {64, "TongueUp"},
    {65, "TongueDown"}, {66, "TongueCurlUp"}, {67, "TongueBendDown"},
    {68, "TongueFlat"}, {69, "TongueRoll"},
};

// 具名函数而不是 lambda：MSVC 14.36 对「捕获数组 + lambda 内下标」会 C1001。
void WriteShapes(const float* values, const ShapeRule* rules, std::size_t count,
                 const fgb::kit::ShapeTable& table, FgbFrameSlot* slot) {
    for (std::size_t i = 0; i < count; ++i) {
        table.Write(slot, rules[i].target, values[rules[i].field]);
    }
}

void DeclareShapeRules(fgb::kit::ShapeTable& table, const ShapeRule* rules, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        table.Declare(rules[i].target);
    }
}

} // namespace

bool ParsePacket(const std::uint8_t* data, std::size_t size, Packet& out, std::string& error) {
    out = Packet{};
    if (data == nullptr) {
        error = "空数据包";
        return false;
    }
    if (size != kPacketBytes) {
        error = "包长 " + std::to_string(size) + " 字节，应为 " + std::to_string(kPacketBytes) +
                " 字节";
        return false;
    }
    if (ReadU32Le(data) != kMagic) {
        error = "魔数不符（前 4 字节小端应为 FFFFFFFD）";
        return false;
    }
    if (ReadU16Le(data + 8) != kMsgTypeBlendshapes) {
        error = "msgType 非 0（不是 BlendShape 包）";
        return false;
    }
    const std::uint16_t payload_size = ReadU16Le(data + 10);
    if (payload_size != kPayloadBytes) {
        error = "payloadSize 为 " + std::to_string(payload_size) + "，应为 " +
                std::to_string(kPayloadBytes);
        return false;
    }

    out.flags = ReadU32Be(data + 4); // Flags 是大端落盘
    out.eye_active = (out.flags & kFlagEye) != 0u;
    out.mouth_active = (out.flags & kFlagMouth) != 0u;

    for (std::size_t i = 0; i < kFieldCount; ++i) {
        const float value = ReadFloatLe(data + kHeaderBytes + i * sizeof(float));
        if (!std::isfinite(value)) { // 上游口径：任一非有限值整包拒收
            error = std::string{"字段 "} + kFieldNames[i] + " 非有限值（NaN/Inf），整包丢弃";
            return false;
        }
        out.values[i] = ClampField(i, value);
    }
    return true;
}

void DeclareTargets(fgb::kit::ShapeTable& table) {
    DeclareShapeRules(table, kEyeShapes, std::size(kEyeShapes));
    DeclareShapeRules(table, kMouthShapes, std::size(kMouthShapes));
}

std::uint64_t WriteFrame(const Packet& packet, const fgb::kit::ShapeTable& table,
                         FgbFrameSlot* slot) {
    std::uint64_t written_attributes = 0;
    const float* values = packet.values.data();

    // 眼域：注视方向、睁眼度、瞳孔，以及眼周/眉部形状。
    if (packet.eye_active) {
        const std::uint64_t eye_attributes = FGB_ATTR_LEFT_OPENNESS | FGB_ATTR_RIGHT_OPENNESS |
                                            FGB_ATTR_LEFT_PUPIL | FGB_ATTR_RIGHT_PUPIL |
                                            FGB_ATTR_LEFT_GAZE | FGB_ATTR_RIGHT_GAZE;
        slot->attributes |= eye_attributes;
        written_attributes |= eye_attributes;

        slot->left_eye.openness = 1.0f - values[kFieldLidCloseLeft];
        slot->left_eye.openness_confidence = 1.0f;
        slot->right_eye.openness = 1.0f - values[kFieldLidCloseRight];
        slot->right_eye.openness_confidence = 1.0f;

        // Pitch/Yaw 是 [-1,1] 归一偏移（+yaw 向右、+pitch 向上），
        // 按正切投影转成 HMD 局部单位向量（-Z 前）。
        fgb::kit::GazeFromLookWeights(values[kFieldEyeYawLeft], values[kFieldEyePitchLeft],
                                      slot->left_eye.gaze_dir);
        fgb::kit::GazeFromLookWeights(values[kFieldEyeYawRight], values[kFieldEyePitchRight],
                                      slot->right_eye.gaze_dir);
        // 眼动来自 Cymple 自有算法，社区未做真机标定核对，置信度按次级来源给出。
        slot->left_eye.gaze_confidence = 0.8f;
        slot->right_eye.gaze_confidence = 0.8f;

        // 上游把 0..1 当作归一化的瞳孔直径（min 0 / max 1）。FGB 的 diameter_mm
        // 需要毫米真值，这里没有依据换算，只写归一扩张度、不写直径。
        slot->left_eye.pupil_diameter_mm = 0.0f;
        slot->right_eye.pupil_diameter_mm = 0.0f;
        slot->left_eye.pupil_dilation = values[kFieldPupilLeft];
        slot->right_eye.pupil_dilation = values[kFieldPupilRight];

        WriteShapes(values, kEyeShapes, std::size(kEyeShapes), table, slot);
        slot->attributes |= FGB_ATTR_EXPRESSION;
        written_attributes |= FGB_ATTR_EXPRESSION;
    }

    if (packet.mouth_active) {
        WriteShapes(values, kMouthShapes, std::size(kMouthShapes), table, slot);
        slot->attributes |= FGB_ATTR_EXPRESSION;
        written_attributes |= FGB_ATTR_EXPRESSION;
    }

    return written_attributes;
}

} // namespace cymple
