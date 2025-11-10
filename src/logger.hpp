#pragma once

#include <array>
#include <cstdio>
#include <ctime>
#include <string_view>

#include "project.h"

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
    "sdmc:/config/" APP_TITLE "/logs.txt";

// Lightweight string builder for log messages
class LogMessage {
   public:
    LogMessage(FILE* file, const char* prefix) : m_file(file) {
        if (m_file && prefix) {
            std::fputs(prefix, m_file);
        }
    }

    // Move constructor
    LogMessage(LogMessage&& other) noexcept : m_file(other.m_file) {
        other.m_file = nullptr;
    }

    // Delete copy operations
    LogMessage(const LogMessage&) = delete;
    LogMessage& operator=(const LogMessage&) = delete;
    LogMessage& operator=(LogMessage&&) = delete;

    ~LogMessage() {
        if (m_file) {
            std::fputc('\n', m_file);
            std::fflush(m_file);
        }
    }

    LogMessage& operator<<(const char* str) {
        if (m_file && str) std::fputs(str, m_file);
        return *this;
    }

    LogMessage& operator<<(std::string_view str) {
        if (m_file) std::fwrite(str.data(), 1, str.size(), m_file);
        return *this;
    }

    LogMessage& operator<<(const std::string& str) {
        return *this << std::string_view(str);
    }

    LogMessage& operator<<(int val) {
        if (m_file) std::fprintf(m_file, "%d", val);
        return *this;
    }

    LogMessage& operator<<(unsigned int val) {
        if (m_file) std::fprintf(m_file, "%u", val);
        return *this;
    }

    LogMessage& operator<<(long val) {
        if (m_file) std::fprintf(m_file, "%ld", val);
        return *this;
    }

    LogMessage& operator<<(unsigned long val) {
        if (m_file) std::fprintf(m_file, "%lu", val);
        return *this;
    }

    LogMessage& operator<<(long long val) {
        if (m_file) std::fprintf(m_file, "%lld", val);
        return *this;
    }

    LogMessage& operator<<(unsigned long long val) {
        if (m_file) std::fprintf(m_file, "%llu", val);
        return *this;
    }

    LogMessage& operator<<(double val) {
        if (m_file) std::fprintf(m_file, "%.6f", val);
        return *this;
    }

    LogMessage& operator<<(bool val) {
        return *this << (val ? "true" : "false");
    }

    // Support for std::endl and other manipulators
    LogMessage& operator<<(std::ostream& (*)(std::ostream&)) {
        // Ignore manipulators, we always add newline in destructor
        return *this;
    }

   private:
    FILE* m_file;
};

class Logger {
   public:
    static Logger& get() noexcept {
        static Logger instance;
        return instance;
    }

    ~Logger() { close(); }

    void truncate() {
        close();
        FILE* f = std::fopen(LOGFILE_PATH.data(), "w");
        if (f) std::fclose(f);
    }

    constexpr void setLevel(LogLevel level) noexcept { m_level = level; }

    void close() {
        if (m_file) {
            std::fclose(m_file);
            m_file = nullptr;
        }
    }

    [[nodiscard]] constexpr bool isEnabled(LogLevel level) const noexcept {
        return level >= m_level;
    }

    LogMessage debug() {
        if (isEnabled(LogLevel::DEBUG)) {
            open();
            return LogMessage(m_file, getPrefix(LogLevel::DEBUG));
        }
        return LogMessage(nullptr, nullptr);
    }

    LogMessage info() {
        if (isEnabled(LogLevel::INFO)) {
            open();
            return LogMessage(m_file, getPrefix(LogLevel::INFO));
        }
        return LogMessage(nullptr, nullptr);
    }

    LogMessage error() {
        if (isEnabled(LogLevel::ERROR)) {
            open();
            return LogMessage(m_file, getPrefix(LogLevel::ERROR));
        }
        return LogMessage(nullptr, nullptr);
    }

    LogMessage none() {
        if (isEnabled(LogLevel::NONE)) {
            open();
            return LogMessage(m_file, getPrefix(LogLevel::NONE));
        }
        return LogMessage(nullptr, nullptr);
    }

   private:
    Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void open() {
        if (!m_file) {
            m_file = std::fopen(LOGFILE_PATH.data(), "a");
            // Set smaller buffer to reduce memory usage
            if (m_file) {
                std::setvbuf(m_file, nullptr, _IOLBF,
                             512);  // 512 bytes line buffer
            }
        }
    }

    static const char* get_time() {
        static std::array<char, 32> buf{};
        u64 now;
        timeGetCurrentTime(TimeType_LocalSystemClock, &now);
        const time_t nowt = now;
        std::strftime(buf.data(), buf.size(), "%F %T UTC", std::gmtime(&nowt));
        return buf.data();
    }

    static const char* getPrefix(LogLevel lvl) {
        static std::array<char, 64> buffer{};

        const char* level_str;
        switch (lvl) {
            case LogLevel::DEBUG:
                level_str = "[DEBUG] ";
                break;
            case LogLevel::INFO:
                level_str = "[INFO ] ";
                break;
            case LogLevel::ERROR:
                level_str = "[ERROR] ";
                break;
            default:
                level_str = "[     ] ";
                break;
        }

        std::snprintf(buffer.data(), buffer.size(), "%s[%s] ", level_str,
                      get_time());
        return buffer.data();
    }

    FILE* m_file = nullptr;
    LogLevel m_level{LogLevel::INFO};
};
