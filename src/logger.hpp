#pragma once

#include <array>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

#include "project.h"

#ifdef DEBUG
#undef DEBUG
#endif

enum class LogLevel : uint8_t {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3,
    NONE = 10,
};

// Custom end-of-line marker to avoid iostream dependency
struct EndLine {};
inline constexpr EndLine endl{};

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
        // Flush only, newline is now handled by std::endl
        if (m_file) {
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

    // Use concepts to handle all signed integral types
    template <std::signed_integral T>
    LogMessage& operator<<(T val) {
        if (m_file) std::fprintf(m_file, "%lld", static_cast<long long>(val));
        return *this;
    }

    // Use concepts to handle all unsigned integral types
    template <std::unsigned_integral T>
    LogMessage& operator<<(T val) {
        if (m_file)
            std::fprintf(m_file, "%llu", static_cast<unsigned long long>(val));
        return *this;
    }

    template <std::floating_point T>
    LogMessage& operator<<(T val) {
        if (m_file) std::fprintf(m_file, "%.6f", static_cast<double>(val));
        return *this;
    }

    LogMessage& operator<<(bool val) {
        return *this << (val ? "true" : "false");
    }

    // Support for custom endl marker (avoids iostream dependency)
    LogMessage& operator<<(EndLine) {
        if (m_file) {
            std::fputc('\n', m_file);
            std::fflush(m_file);
        }
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
        return std::to_underlying(level) >= std::to_underlying(m_level);
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

    LogMessage warn() {
        if (isEnabled(LogLevel::WARN)) {
            open();
            return LogMessage(m_file, getPrefix(LogLevel::WARN));
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
            case LogLevel::WARN:
                level_str = "[WARN ] ";
                break;
            case LogLevel::ERROR:
                level_str = "[ERROR] ";
                break;
            case LogLevel::NONE:
                level_str = "";
                break;
            default:
                level_str = "[     ] ";
                break;
        }

        std::snprintf(buffer.data(), buffer.size(), "%s", level_str);
        return buffer.data();
    }

    FILE* m_file = nullptr;
    LogLevel m_level{LogLevel::INFO};
};
