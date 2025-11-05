#pragma once

#include <switch.h>

#include <string>
#include <string_view>
#include <unordered_map>

enum class UploadMode : uint8_t {
    Compressed = 0,  // 只上传压缩图
    Original = 1,    // 只上传原图
    Both = 2         // 上传两者（先压缩，失败则原图）
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
    [[nodiscard]] bool uploadAllowed(std::string_view tid,
                                     bool isMovie) const noexcept;
    [[nodiscard]] constexpr bool keepLogs() const noexcept {
        return m_keepLogs;
    }
    [[nodiscard]] constexpr UploadMode getUploadMode() const noexcept {
        return m_uploadMode;
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
    UploadMode m_uploadMode{UploadMode::Compressed};  // 默认使用压缩模式
    std::unordered_map<std::string, bool> m_titleScreenshots;
    std::unordered_map<std::string, bool> m_titleMovies;
};
