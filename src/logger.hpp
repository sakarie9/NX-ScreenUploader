#pragma once

#include <array>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string_view>

#ifdef DEBUG
#undef DEBUG
#endif

enum class LogLevel : uint8_t {
    DEBUG = 0,
    INFO = 1,
    ERROR = 2,
    NONE = 10,
};

inline constexpr std::string_view LOGFILE_PATH =
    "sdmc:/config/sys-screen-capture-uploader/logs.txt";

class Logger {
   public:
    static Logger& get() noexcept {
        static Logger instance;
        return instance;
    }

    void truncate() {
        close();
        m_file.open(LOGFILE_PATH.data(), std::ios::trunc);
        close();
    }

    constexpr void setLevel(LogLevel level) noexcept { m_level = level; }

    void open() {
        if (!m_file.is_open()) {
            m_file.open(LOGFILE_PATH.data(), std::ios::app);
        }
    }

    void close() {
        if (m_file.is_open()) {
            m_file.close();
        }
    }

    [[nodiscard]] constexpr bool isEnabled(LogLevel level) const noexcept {
        return level >= m_level;
    }

    std::ostream& debug() {
        if (isEnabled(LogLevel::DEBUG)) {
            open();
            m_file << getPrefix(LogLevel::DEBUG);
            return m_file;
        }
        return std::cout;
    }

    std::ostream& info() {
        if (isEnabled(LogLevel::INFO)) {
            open();
            m_file << getPrefix(LogLevel::INFO);
            return m_file;
        }
        return std::cout;
    }

    std::ostream& error() {
        if (isEnabled(LogLevel::ERROR)) {
            open();
            m_file << getPrefix(LogLevel::ERROR);
            return m_file;
        }
        return std::cout;
    }

    std::ostream& none() {
        if (isEnabled(LogLevel::NONE)) {
            open();
            m_file << getPrefix(LogLevel::NONE);
            return m_file;
        }
        return std::cout;
    }

   private:
    Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    static std::string get_time() {
        u64 now;
        timeGetCurrentTime(TimeType_LocalSystemClock, &now);
        const time_t nowt = now;
        std::array<char, 32> buf{};
        std::strftime(buf.data(), buf.size(), "%F %T UTC", std::gmtime(&nowt));
        return std::string(buf.data());
    }

    static std::string getPrefix(LogLevel lvl) {
        constexpr std::string_view debugPrefix = "[DEBUG] ";
        constexpr std::string_view infoPrefix = "[INFO ] ";
        constexpr std::string_view errorPrefix = "[ERROR] ";
        constexpr std::string_view defaultPrefix = "[     ] ";

        std::string_view prefix;
        switch (lvl) {
            case LogLevel::DEBUG:
                prefix = debugPrefix;
                break;
            case LogLevel::INFO:
                prefix = infoPrefix;
                break;
            case LogLevel::ERROR:
                prefix = errorPrefix;
                break;
            default:
                prefix = defaultPrefix;
                break;
        }

        std::string result;
        result.reserve(prefix.size() + 32);
        result += prefix;
        result += "[";
        result += get_time();
        result += "] ";
        return result;
    }

    std::ofstream m_file;
    LogLevel m_level{LogLevel::INFO};
};
