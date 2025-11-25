#pragma once

#include <switch.h>

#include <string>
#include <string_view>

#include "config_defaults.hpp"

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

    // Upload destination toggles
    [[nodiscard]] constexpr bool telegramEnabled() const noexcept {
        return m_telegramEnabled;
    }
    [[nodiscard]] constexpr bool ntfyEnabled() const noexcept {
        return m_ntfyEnabled;
    }

    // Telegram configuration
    [[nodiscard]] std::string_view getTelegramBotToken() const noexcept;
    [[nodiscard]] std::string_view getTelegramChatId() const noexcept;
    [[nodiscard]] std::string_view getTelegramApiUrl() const noexcept;
    [[nodiscard]] constexpr bool telegramUploadScreenshots() const noexcept {
        return m_telegramUploadScreenshots;
    }
    [[nodiscard]] constexpr bool telegramUploadMovies() const noexcept {
        return m_telegramUploadMovies;
    }
    [[nodiscard]] std::string_view getTelegramUploadMode() const noexcept {
        return m_telegramUploadMode;
    }

    // Ntfy configuration
    [[nodiscard]] std::string_view getNtfyUrl() const noexcept;
    [[nodiscard]] std::string_view getNtfyTopic() const noexcept;
    [[nodiscard]] std::string_view getNtfyToken() const noexcept;
    [[nodiscard]] std::string_view getNtfyPriority() const noexcept;
    [[nodiscard]] constexpr bool ntfyUploadScreenshots() const noexcept {
        return m_ntfyUploadScreenshots;
    }
    [[nodiscard]] constexpr bool ntfyUploadMovies() const noexcept {
        return m_ntfyUploadMovies;
    }

    bool error{false};

   private:
    Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    // Upload destination toggles
    bool m_telegramEnabled{ConfigDefaults::TELEGRAM_ENABLED};
    bool m_ntfyEnabled{ConfigDefaults::NTFY_ENABLED};

    // Telegram configuration
    std::string m_telegramBotToken{ConfigDefaults::TELEGRAM_BOT_TOKEN};
    std::string m_telegramChatId{ConfigDefaults::TELEGRAM_CHAT_ID};
    std::string m_telegramApiUrl{ConfigDefaults::TELEGRAM_API_URL};
    bool m_telegramUploadScreenshots{
        ConfigDefaults::TELEGRAM_UPLOAD_SCREENSHOTS};
    bool m_telegramUploadMovies{ConfigDefaults::TELEGRAM_UPLOAD_MOVIES};
    std::string m_telegramUploadMode{ConfigDefaults::TELEGRAM_UPLOAD_MODE};

    // Ntfy configuration
    std::string m_ntfyUrl{ConfigDefaults::NTFY_URL};
    std::string m_ntfyTopic{ConfigDefaults::NTFY_TOPIC};
    std::string m_ntfyToken{ConfigDefaults::NTFY_TOKEN};
    std::string m_ntfyPriority{ConfigDefaults::NTFY_PRIORITY};
    bool m_ntfyUploadScreenshots{ConfigDefaults::NTFY_UPLOAD_SCREENSHOTS};
    bool m_ntfyUploadMovies{ConfigDefaults::NTFY_UPLOAD_MOVIES};

    // General settings
    bool m_keepLogs{ConfigDefaults::KEEP_LOGS};
    int m_checkIntervalSeconds{ConfigDefaults::CHECK_INTERVAL_SECONDS};
};
