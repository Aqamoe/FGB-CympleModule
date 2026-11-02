// Cymple（Project Cymple）→ FaceGaze Bridge 原生模块。
//
// 监听 Cymple PC 端发到本机 22999 端口的 292 字节 UDP 包（VRCFT 兼容格式），
// 按包内 Flags 分眼域/嘴域输出：注视方向、睁眼度、瞳孔扩张度，
// 以及 68 个 Unified Expressions 形状目标（眼域 12 + 嘴域 56）。不需要 VRCFT，不注入任何进程，也不需要管理员权限。

#include "cymple_payload.hpp"
#include "cymple_wire.hpp"

#include "fgb/kit/config.hpp"
#include "fgb/kit/module.hpp"

#include <cstdint>
#include <string>

namespace {

constexpr std::uint16_t kDefaultPort = cymple::kDefaultPort;
constexpr std::uint64_t kNoDataTimeoutNs = 10ull * 1000ull * 1000ull * 1000ull;
constexpr std::uint64_t kWarnIntervalNs = 5ull * 1000ull * 1000ull * 1000ull;

class CympleSource : public fgb::kit::IModuleSource {
public:
    bool Start(fgb::kit::Host& host, const FgbInitParams& params, std::string& error) override {
        fgb::kit::ConfigIni config;
        std::string config_error;
        const std::string directory = params.module_dir != nullptr ? params.module_dir : "";
        config.AllowKeys({"port"});
        if (config.Load(directory, "config.ini", config_error)) {
            port_ = static_cast<std::uint16_t>(config.GetInt("port", port_));
            if (!config_error.empty()) {
                host.Warn("config.ini：" + config_error);
            }
            if (!config.UnknownKeys().empty()) {
                host.Warn("config.ini 有未识别的键：" + config.UnknownKeys()[0]);
            }
        }

        fgb::kit::ShapeTable table;
        cymple::DeclareTargets(table);
        if (!table.Build(host)) {
            error = "宿主没有提供 shape_index，无法映射 Unified Expressions";
            return false;
        }
        if (table.ResolvedCount() < table.Size()) {
            host.Warn("部分 Unified Expressions 形状未被宿主识别，已跳过 " +
                      std::to_string(table.Size() - table.ResolvedCount()) + " 个");
        }
        table_ = std::move(table);

        if (!endpoint_.Bind(port_, error)) {
            return false;
        }
        host.Info("Cymple 监听 UDP " + std::to_string(port_) + "，等待 Cymple 端发送");
        host.StatusStarting("等待 Cymple 数据");
        return true;
    }

    bool Read(fgb::kit::Host& host, FgbFrameSlot& slot, std::uint64_t now_ns) override {
        cymple::Packet packet;
        bool parsed = false;

        fgb::kit::UdpPacket datagram;
        // Cymple 按帧率推送，只处理最新一包；积压的旧包丢掉比逐帧回放更准。
        if (endpoint_.PollLatest(datagram)) {
            std::string error;
            if (cymple::ParsePacket(datagram.data.data(), datagram.data.size(), packet, error)) {
                parsed = true;
                if (!packet.eye_active && !packet.mouth_active) {
                    host.WarnThrottled(flags_ns_, now_ns, kWarnIntervalNs,
                                       "收到 Flags 未点亮眼/嘴域的包，已忽略（检查 Cymple 是否开启了输出）");
                }
                if ((packet.flags & ~cymple::kFlagKnown) != 0u) {
                    host.WarnThrottled(unknown_ns_, now_ns, kWarnIntervalNs,
                                       "包内出现未定义的 Flags 位，语义未确认，本模块不据此改变行为");
                }
            } else {
                host.WarnThrottled(warn_ns_, now_ns, kWarnIntervalNs, "丢弃一包：" + error);
            }
        }

        if (!staleness_.Check(host, endpoint_.LastPacketNs(), now_ns) || !parsed) {
            return false;
        }

        return cymple::WriteFrame(packet, table_, &slot) != 0u;
    }

    void Stop(fgb::kit::Host& host) override {
        endpoint_.Close();
        host.StatusStopped("Cymple 已停止");
    }

private:
    fgb::kit::UdpEndpoint endpoint_;
    fgb::kit::ShapeTable table_;
    fgb::kit::Staleness staleness_{kNoDataTimeoutNs, "Cymple"};
    std::uint16_t port_ = kDefaultPort;
    std::uint64_t warn_ns_ = 0;
    std::uint64_t flags_ns_ = 0;
    std::uint64_t unknown_ns_ = 0;
};

} // namespace

FGB_KIT_EXPORT(CympleSource, "com.fgbbridge.cymple", "Cymple Face Tracking",
               FGB_CAP_EXPRESSION | FGB_CAP_EYES)
