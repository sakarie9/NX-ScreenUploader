#pragma once

#include <switch.h>

#include <string>
#include <string_view>
#include <unordered_map>

class Config {
   public:
    static Config& get() noexcept {
        static Config instance;
        return instance;
    }

    [[nodiscard]] bool refresh();

    [[nodiscard]] std::string_view getTelegramBotToken() const noexcept;
    [[nodiscard]] std::string_view getTelegramChatId() const noexcept;
    [[nodiscard]] bool uploadAllowed(std::string_view tid,
                                     bool isMovie) const noexcept;
    [[nodiscard]] constexpr bool keepLogs() const noexcept {
        return m_keepLogs;
    }

    bool error{false};

   private:
    Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    std::string m_telegramBotToken;
    std::string m_telegramChatId;
    bool m_uploadScreenshots{true};
    bool m_uploadMovies{true};
    bool m_keepLogs{false};
    std::unordered_map<std::string, bool> m_titleScreenshots;
    std::unordered_map<std::string, bool> m_titleMovies;
};
