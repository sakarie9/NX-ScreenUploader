#pragma once

#include <switch.h>

#include <string>
#include <string_view>

enum class UploadMode : uint8_t {
    Compressed = 0,  // Upload only compressed images
    Original = 1,    // Upload only original images
    Both = 2  // Upload both (try compressed first, then original on failure)
};

class Config {
   public:
    static Config& get() noexcept {
        static Config instance;
        return instance;
    }

    [[nodiscard]] bool refresh();

    [[nodiscard]] std::string_view getTelegramBotToken() const noexcept;
    [[nodiscard]] std::string_view getTelegramChatId() const noexcept;
    [[nodiscard]] std::string_view getTelegramApiUrl() const noexcept;
    [[nodiscard]] constexpr bool uploadScreenshots() const noexcept {
        return m_uploadScreenshots;
    }
    [[nodiscard]] constexpr bool uploadMovies() const noexcept {
        return m_uploadMovies;
    }
    [[nodiscard]] constexpr bool keepLogs() const noexcept {
        return m_keepLogs;
    }
    [[nodiscard]] constexpr UploadMode getUploadMode() const noexcept {
        return m_uploadMode;
    }
    [[nodiscard]] constexpr int getCheckIntervalSeconds() const noexcept {
        return m_checkIntervalSeconds;
    }

    bool error{false};

   private:
    Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    std::string m_telegramBotToken;
    std::string m_telegramChatId;
    std::string m_telegramApiUrl;
    bool m_uploadScreenshots{true};
    bool m_uploadMovies{true};
    bool m_keepLogs{false};
    UploadMode m_uploadMode{
        UploadMode::Compressed};  // Default to compressed mode
    int m_checkIntervalSeconds{
        3};  // Check interval (seconds), default 3s, minimum 1s
};
