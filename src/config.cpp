#include "config.hpp"

#include <inih.h>

#include <iostream>
#include <set>

#include "logger.hpp"
#include "utils.hpp"

using namespace std;

bool Config::refresh() {
    INIReader reader("sdmc:/config/sys-screen-capture-uploader/config.ini");

    if (reader.ParseError() != 0) {
        Logger::get().error()
            << "Config parse error " << reader.ParseError() << endl;
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
        for (auto &tid : reader.Keys("title_screenshots")) {
            m_titleScreenshots[tid] =
                reader.GetBoolean("title_screenshots", tid, true);
        }
    }

    if (reader.HasSection("title_movies")) {
        for (auto &tid : reader.Keys("title_movies")) {
            m_titleMovies[tid] = reader.GetBoolean("title_movies", tid, true);
        }
    }

    return true;
}

string Config::getTelegramBotToken() { return m_telegramBotToken; }

string Config::getTelegramChatId() { return m_telegramChatId; }

bool Config::uploadAllowed(string &tid, bool isMovie) {
    if (isMovie) {
        if (m_titleMovies.count(tid) > 0) return m_titleMovies[tid];
        return m_uploadMovies;
    } else {
        if (m_titleScreenshots.count(tid) > 0) return m_titleScreenshots[tid];
        return m_uploadScreenshots;
    }
}

bool Config::keepLogs() { return m_keepLogs; }
