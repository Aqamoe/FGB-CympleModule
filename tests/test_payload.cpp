#include "cymple_payload.hpp"
#include "cymple_wire.hpp"

#include "fgb/kit/testing/check.hpp"
#include "fgb/kit/testing/host_mock.hpp"

#include <cmath>
#include <cstring>
#include <limits>
#include <string>

using namespace fgb::kit;
using cymple::Packet;
using cymple::ParsePacket;

namespace {

std::size_t Field(const char* name) {
    for (std::size_t i = 0; i < cymple::kFieldCount; ++i) {
        if (std::strcmp(cymple::kFieldNames[i], name) == 0) {
            return i;
        }
    }
    return cymple::kFieldCount;  // 找不到时越界标记，由调用方断言失败
}

// 与上游回归夹具同款的「逐位可辨」数据：第 i 个字段 = (i+1)/100，
// 前 4 个换成有符号的代表值，瞳孔给 0..1 内的两个不同值。
std::array<float, cymple::kFieldCount> Values() {
    std::array<float, cymple::kFieldCount> values{};
    for (std::size_t i = 0; i < cymple::kFieldCount; ++i) {
        values[i] = static_cast<float>(i + 1) / 100.0f;
    }
    values[0] = -0.31f;  // EyePitch_L
    values[1] = 0.29f;   // EyePitch_R
    values[2] = -0.37f;  // EyeYaw_L
    values[3] = 0.41f;   // EyeYaw_R
    values[4] = 0.62f;   // Eye_Pupil_Left
    values[5] = 0.58f;   // Eye_Pupil_Right
    return values;
}

FgbFrameSlot FreshSlot() {
    FgbFrameSlot slot{};
    slot.struct_size = sizeof(FgbFrameSlot);
    slot.abi_version = FGB_MODULE_ABI_V2;
    slot.shape_count = FGB_MAX_SHAPES;
    return slot;
}

bool Parse(const std::uint8_t (&bytes)[cymple::kPacketBytes], std::size_t size, Packet& out,
           std::string& error) {
    return ParsePacket(bytes, size, out, error);
}

} // namespace

int main() {
    // ---- 正常包：70 个字段逐一解码 ----
    std::array<float, cymple::kFieldCount> values = Values();
    std::uint8_t bytes[cymple::kPacketBytes]{};
    cymple::BuildPacket(bytes, cymple::kFlagMouth | cymple::kFlagEye, values);

    Packet packet{};
    std::string error;
    CHECK(Parse(bytes, sizeof(bytes), packet, error));
    CHECK(packet.eye_active);
    CHECK(packet.mouth_active);
    CHECK_NEAR(packet.values[0], -0.31f, 1e-6);
    CHECK_NEAR(packet.values[31], 0.32f, 1e-6);          // JawOpen
    CHECK_NEAR(packet.values[Field("LipSuckUL")], 0.38f, 1e-6);
    CHECK_NEAR(packet.values[Field("TongueRoll")], 0.70f, 1e-6);
    CHECK_EQ(Field("JawOpen"), static_cast<std::size_t>(31));

    // ---- 字节序：Flags 大端、魔数与 float 小端 ----
    // 大端 0x00000003 落盘 [4..7] = 00 00 00 03；若按小端读会得 0x30000000（无有效域）。
    CHECK_EQ(bytes[4], static_cast<std::uint8_t>(0x00));
    CHECK_EQ(bytes[7], static_cast<std::uint8_t>(0x03));
    CHECK_EQ(bytes[0], static_cast<std::uint8_t>(0xFD));  // 魔数小端 FD FF FF FF
    CHECK_EQ(bytes[1], static_cast<std::uint8_t>(0xFF));
    CHECK_EQ(bytes[3], static_cast<std::uint8_t>(0xFF));
    CHECK_EQ(bytes[11], static_cast<std::uint8_t>(0x01)); // payloadSize 280 小端 = 18 01
    CHECK_NEAR(packet.values[4], 0.62f, 1e-6);            // float 必须按小端读出原值

    // ---- 长度不足 / 超长 ----
    Packet bad{};
    std::uint8_t short11[cymple::kPacketBytes]{};
    std::memcpy(short11, bytes, sizeof(bytes));
    CHECK(!Parse(short11, 11, bad, error));
    CHECK(!Parse(short11, 12, bad, error));   // 只有头没有负载也必须拒
    CHECK(!Parse(short11, cymple::kPacketBytes - 1, bad, error));
    CHECK(!Parse(short11, cymple::kPacketBytes + 1, bad, error));
    CHECK(!Parse(short11, 0, bad, error));

    // ---- 前缀 / msgType / payloadSize 不符 ----
    std::uint8_t probe[cymple::kPacketBytes]{};
    cymple::BuildPacket(probe, 3, values);
    probe[0] = 0xFC;  // 魔数被改
    CHECK(!Parse(probe, sizeof(probe), bad, error));

    cymple::BuildPacket(probe, 3, values);
    probe[0] = 0xFF; probe[1] = 0xFF; probe[2] = 0xFF; probe[3] = 0xFD;  // 魔数写成大端
    CHECK(!Parse(probe, sizeof(probe), bad, error));

    cymple::BuildPacket(probe, 3, values);
    probe[8] = 1;  // msgType != 0
    CHECK(!Parse(probe, sizeof(probe), bad, error));

    cymple::BuildPacket(probe, 3, values);
    probe[10] = 0x17;  // payloadSize 279
    CHECK(!Parse(probe, sizeof(probe), bad, error));

    cymple::BuildPacket(probe, 3, values);
    probe[10] = 0x01; probe[11] = 0x18;  // 280 写成大端形态 → 数值 6145，必须拒
    CHECK(!Parse(probe, sizeof(probe), bad, error));

    cymple::BuildPacket(probe, 3, values);
    CHECK(Parse(probe, sizeof(probe), bad, error));  // 复原后必须能解析，防止夹具自身出错

    // ---- NaN / Inf 清洗：任一字段非有限都整包拒收 ----
    for (std::size_t i = 0; i < cymple::kFieldCount; ++i) {
        cymple::BuildPacket(bytes, 3, values);
        cymple::PutFloat(bytes + cymple::kHeaderBytes, i, std::nanf(""));
        CHECK(!Parse(bytes, sizeof(bytes), bad, error));
        cymple::PutFloat(bytes + cymple::kHeaderBytes, i,
                         i % 2 == 0 ? -std::numeric_limits<float>::infinity()
                                    : std::numeric_limits<float>::infinity());
        CHECK(!Parse(bytes, sizeof(bytes), bad, error));
    }

    // ---- 越界清洗：Pitch/Yaw 裁剪到 [-1,1]，其余裁剪到 [0,1] ----
    std::array<float, cymple::kFieldCount> wild = values;
    wild[0] = -2.5f;             // pitch 越下界
    wild[2] = 3.0f;              // yaw 越上界
    wild[31] = 1.5f;             // JawOpen 越界
    wild[cymple::kFieldPupilLeft] = -0.5f;
    cymple::BuildPacket(bytes, 3, wild);
    Packet cleaned{};
    CHECK(Parse(bytes, sizeof(bytes), cleaned, error));
    CHECK_NEAR(cleaned.values[0], -1.0f, 1e-6);
    CHECK_NEAR(cleaned.values[2], 1.0f, 1e-6);
    CHECK_NEAR(cleaned.values[31], 1.0f, 1e-6);
    CHECK_NEAR(cleaned.values[cymple::kFieldPupilLeft], 0.0f, 1e-6);

    // ---- 帧写入：通用映射 + 双眼属性 ----
    testing::HostMock host;
    Host logger{host.services()};
    ShapeTable table;
    cymple::DeclareTargets(table);
    CHECK(table.Build(logger));
    CHECK_EQ(table.ResolvedCount(), table.Size());  // 目标名必须都是真实形状

    FgbFrameSlot slot = FreshSlot();
    CHECK(cymple::WriteFrame(packet, table, &slot) != 0u);

    CHECK((slot.attributes & FGB_ATTR_EXPRESSION) != 0);
    CHECK((slot.attributes & FGB_ATTR_LEFT_OPENNESS) != 0);
    CHECK((slot.attributes & FGB_ATTR_RIGHT_GAZE) != 0);
    CHECK((slot.attributes & FGB_ATTR_LEFT_PUPIL) != 0);

    CHECK_NEAR(slot.shapes[host.IndexOf("JawOpen")], 0.32f, 1e-6);
    // 一源双目标：MouthSmileLeft 同时喂 CornerPull 与 CornerSlant
    CHECK_NEAR(slot.shapes[host.IndexOf("MouthCornerPullLeft")], 0.52f, 1e-6);
    CHECK_NEAR(slot.shapes[host.IndexOf("MouthCornerSlantLeft")], 0.52f, 1e-6);
    // 改名项：JawFwd → JawForward，LipSuckUL → LipSuckUpperLeft
    CHECK_NEAR(slot.shapes[host.IndexOf("JawForward")], 0.31f, 1e-6);
    CHECK_NEAR(slot.shapes[host.IndexOf("LipSuckUpperLeft")], 0.38f, 1e-6);
    // 眼域形状（眉部）来自前 20 个字段
    CHECK_NEAR(slot.shapes[host.IndexOf("BrowPinchLeft")], 0.19f, 1e-6);
    CHECK(slot.shape_valid[host.IndexOf("TongueSquish")] == 0);  // 包里根本没有的槽位

    // 睁眼度 = 1 - EyeLidClose
    CHECK_NEAR(slot.left_eye.openness, 1.0f - values[cymple::kFieldLidCloseLeft], 1e-6);
    // 瞳孔：只写归一扩张度，不伪造毫米直径
    CHECK_NEAR(slot.left_eye.pupil_dilation, 0.62f, 1e-6);
    CHECK_NEAR(slot.left_eye.pupil_diameter_mm, 0.0f, 1e-6);

    // 注视方向必须是单位向量、-Z 前；+yaw 向右（+x）、+pitch 向上（+y）
    const float length = std::sqrt(slot.left_eye.gaze_dir[0] * slot.left_eye.gaze_dir[0] +
                                   slot.left_eye.gaze_dir[1] * slot.left_eye.gaze_dir[1] +
                                   slot.left_eye.gaze_dir[2] * slot.left_eye.gaze_dir[2]);
    CHECK_NEAR(length, 1.0, 1e-5);
    CHECK(slot.left_eye.gaze_dir[2] < 0.0f);
    CHECK(slot.left_eye.gaze_dir[0] < 0.0f);  // 左眼 yaw=-0.37 → 向左
    CHECK(slot.left_eye.gaze_dir[1] < 0.0f);  // 左眼 pitch=-0.31 → 向下
    CHECK(slot.right_eye.gaze_dir[0] > 0.0f); // 右眼 yaw=+0.41 → 向右
    CHECK(slot.right_eye.gaze_dir[1] > 0.0f); // 右眼 pitch=+0.29 → 向上

    // ---- 未确认/未点亮的域不得伪造数据 ----
    // Flags 没有任何已知域位：解析成功，但一帧都不许写。
    cymple::BuildPacket(bytes, 0, values);
    Packet idle{};
    CHECK(Parse(bytes, sizeof(bytes), idle, error));
    CHECK(!idle.eye_active);
    CHECK(!idle.mouth_active);
    FgbFrameSlot empty = FreshSlot();
    CHECK_EQ(cymple::WriteFrame(idle, table, &empty), static_cast<std::uint64_t>(0));
    CHECK_EQ(empty.attributes, static_cast<std::uint64_t>(0));

    // 把 0x03 误写成小端落盘（= 0x03000000 大端）：域位不亮，同样不许写。
    std::uint8_t swapped[cymple::kPacketBytes]{};
    cymple::BuildPacket(swapped, 3, values);
    swapped[4] = 0x03; swapped[5] = 0x00; swapped[6] = 0x00; swapped[7] = 0x00;
    Packet le_flags{};
    CHECK(Parse(swapped, sizeof(swapped), le_flags, error));
    CHECK(!le_flags.eye_active);
    CHECK(!le_flags.mouth_active);
    FgbFrameSlot swapped_slot = FreshSlot();
    CHECK_EQ(cymple::WriteFrame(le_flags, table, &swapped_slot), static_cast<std::uint64_t>(0));

    // 只有眼域（flags=2）：嘴部槽位必须保持 invalid，而不是被当成 0 写入。
    cymple::BuildPacket(bytes, cymple::kFlagEye, values);
    Packet eye_only{};
    CHECK(Parse(bytes, sizeof(bytes), eye_only, error));
    FgbFrameSlot eye_slot = FreshSlot();
    CHECK(cymple::WriteFrame(eye_only, table, &eye_slot) != 0u);
    CHECK((eye_slot.attributes & FGB_ATTR_LEFT_GAZE) != 0);
    CHECK(eye_slot.shape_valid[host.IndexOf("JawOpen")] == 0);

    // 只有嘴域（flags=1）：不得伪造注视/睁眼度/瞳孔。
    cymple::BuildPacket(bytes, cymple::kFlagMouth, values);
    Packet mouth_only{};
    CHECK(Parse(bytes, sizeof(bytes), mouth_only, error));
    FgbFrameSlot mouth_slot = FreshSlot();
    CHECK(cymple::WriteFrame(mouth_only, table, &mouth_slot) != 0u);
    CHECK((mouth_slot.attributes & FGB_ATTR_EXPRESSION) != 0);
    CHECK((mouth_slot.attributes & (FGB_ATTR_LEFT_GAZE | FGB_ATTR_RIGHT_GAZE |
                                    FGB_ATTR_LEFT_OPENNESS | FGB_ATTR_RIGHT_OPENNESS |
                                    FGB_ATTR_LEFT_PUPIL | FGB_ATTR_RIGHT_PUPIL)) == 0);
    CHECK_NEAR(mouth_slot.shapes[host.IndexOf("JawOpen")], 0.32f, 1e-6);

    return CheckSummary("cymple-payload");
}
