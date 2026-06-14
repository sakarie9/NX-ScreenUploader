#pragma once

#include <string>
#include <string_view>

// Include all channel headers via the unified list
#define CHANNEL_INCLUDES
#include "channels/channels.inc"
#undef CHANNEL_INCLUDES

class Config {
   public:
    static Config& get() noexcept {
        static Config instance;
        return instance;
    }

    [[nodiscard]] bool refresh();

    // General settings
    [[nodiscard]] constexpr int getCheckIntervalSeconds() const noexcept {
        return m_checkIntervalSeconds;
    }
    [[nodiscard]] constexpr bool keepLogs() const noexcept {
        return m_keepLogs;
    }
    [[nodiscard]] std::string_view getLogLevel() const noexcept {
        return m_logLevel;
    }
    [[nodiscard]] constexpr bool pngshotEnabled() const noexcept {
        return m_pngshotEnabled;
    }

// Per-channel configuration (defined in channels/channels.inc)
#define CHANNEL(Ns, M) Ns##Channel::Config M;
#include "channels/channels.inc"
#undef CHANNEL

    bool error{false};

   private:
    Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    // General settings
    int m_checkIntervalSeconds{5};
    bool m_keepLogs{false};
    std::string m_logLevel{"info"};
    bool m_pngshotEnabled{false};
};