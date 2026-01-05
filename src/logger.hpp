#pragma once

#include <array>
#include <mutex>
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

// Forward declaration
class Logger;

// Lightweight string builder for log messages
class LogMessage {
   public:
    LogMessage(FILE* file, const char* prefix,
               std::unique_lock<std::mutex>&& lock)
        : m_file(file), m_lock(std::move(lock)) {
        if (m_file && prefix) {
            std::fputs(prefix, m_file);
        }
    }

    // Default constructor for disabled logs
    LogMessage() : m_file(nullptr), m_lock() {}

    // Move constructor
    LogMessage(LogMessage&& other) noexcept
        : m_file(other.m_file), m_lock(std::move(other.m_lock)) {
        other.m_file = nullptr;
    }

    // Delete copy operations
    LogMessage(const LogMessage&) = delete;
    LogMessage& operator=(const LogMessage&) = delete;
    LogMessage& operator=(LogMessage&&) = delete;

    ~LogMessage() {
        // Close file after each message and release lock
        if (m_file) {
            std::fflush(m_file);
            std::fclose(m_file);
            m_file = nullptr;
        }
        // Lock automatically released when m_lock goes out of scope
    }

    friend class Logger;  // Allow Logger to access our private members if
                          // needed

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
            // File will be closed in destructor
        }
        return *this;
    }

   private:
    FILE* m_file;
    std::unique_lock<std::mutex> m_lock;
};

class Logger {
   public:
    static Logger& get() noexcept {
        static Logger instance;
        return instance;
    }

    ~Logger() { close(); }

    void truncate() {
        std::unique_lock<std::mutex> lock(m_mutex);
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
            auto lock = acquireLock();
            open();
            return LogMessage(m_file, getPrefix(LogLevel::DEBUG),
                              std::move(lock));
        }
        return LogMessage();
    }

    LogMessage info() {
        if (isEnabled(LogLevel::INFO)) {
            auto lock = acquireLock();
            open();
            return LogMessage(m_file, getPrefix(LogLevel::INFO),
                              std::move(lock));
        }
        return LogMessage();
    }

    LogMessage warn() {
        if (isEnabled(LogLevel::WARN)) {
            auto lock = acquireLock();
            open();
            return LogMessage(m_file, getPrefix(LogLevel::WARN),
                              std::move(lock));
        }
        return LogMessage();
    }

    LogMessage error() {
        if (isEnabled(LogLevel::ERROR)) {
            auto lock = acquireLock();
            open();
            return LogMessage(m_file, getPrefix(LogLevel::ERROR),
                              std::move(lock));
        }
        return LogMessage();
    }

    LogMessage none() {
        if (isEnabled(LogLevel::NONE)) {
            auto lock = acquireLock();
            open();
            return LogMessage(m_file, getPrefix(LogLevel::NONE),
                              std::move(lock));
        }
        return LogMessage();
    }

   private:
    Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::unique_lock<std::mutex> acquireLock() {
        return std::unique_lock<std::mutex>(m_mutex);
    }

    void open() {
        if (!m_file) {
            m_file = std::fopen(LOGFILE_PATH.data(), "a");
            // Use unbuffered mode for real-time log viewing
            if (m_file) {
                std::setvbuf(m_file, nullptr, _IONBF,
                             0);  // No buffering - logs visible immediately
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
    std::mutex m_mutex;
};

// Implementation of unused clearLoggerFile() removed as it's no longer needed
