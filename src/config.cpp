#include "config.hpp"

#include <inih.h>

#include <iostream>

#include "logger.hpp"
#include "utils.hpp"

bool Config::refresh() {
    INIReader reader("sdmc:/config/sys-screen-capture-uploader/config.ini");

    if (const int parseError = reader.ParseError(); parseError != 0) {
        Logger::get().error()
            << "Config parse error " << parseError << std::endl;
        error = true;
        return false;
    }

    m_telegramBotToken =
        reader.Get("general", "telegram_bot_token", "undefined");
    m_telegramChatId = reader.Get("general", "telegram_chat_id", "undefined");
    m_uploadScreenshots =
        reader.GetBoolean("general", "upload_screenshots", true);
    m_uploadMovies = reader.GetBoolean("general", "upload_movies", true);
    m_keepLogs = reader.GetBoolean("general", "keep_logs", false);

    if (reader.HasSection("title_screenshots")) {
        const auto keys = reader.Keys("title_screenshots");
        m_titleScreenshots.reserve(keys.size());
        for (const auto& tid : keys) {
            m_titleScreenshots.emplace(
                tid, reader.GetBoolean("title_screenshots", tid, true));
        }
    }

    if (reader.HasSection("title_movies")) {
        const auto keys = reader.Keys("title_movies");
        m_titleMovies.reserve(keys.size());
        for (const auto& tid : keys) {
            m_titleMovies.emplace(tid,
                                  reader.GetBoolean("title_movies", tid, true));
        }
    }

    return true;
}

std::string_view Config::getTelegramBotToken() const noexcept {
    return m_telegramBotToken;
}

std::string_view Config::getTelegramChatId() const noexcept {
    return m_telegramChatId;
}

bool Config::uploadAllowed(std::string_view tid, bool isMovie) const noexcept {
    if (isMovie) {
        if (const auto it = m_titleMovies.find(std::string(tid));
            it != m_titleMovies.end()) {
            return it->second;
        }
        return m_uploadMovies;
    } else {
        if (const auto it = m_titleScreenshots.find(std::string(tid));
            it != m_titleScreenshots.end()) {
            return it->second;
        }
        return m_uploadScreenshots;
    }
}
