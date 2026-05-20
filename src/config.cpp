#include "config.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <string_view>

#include "channels/ini_helpers.hpp"
#include "logger.hpp"
#include "project.h"

// Configuration file path
static constexpr const char* CONFIG_PATH =
    "sdmc:/config/" APP_TITLE "/config.ini";

// Helper: check if config file exists
static bool configFileExists() {
    struct stat buffer;
    return stat(CONFIG_PATH, &buffer) == 0;
}

// General settings defaults (kept local as they are not channel-specific)
namespace {
constexpr int DEFAULT_CHECK_INTERVAL_SECONDS = 5;
constexpr int CHECK_INTERVAL_MINIMUM = 1;
constexpr bool DEFAULT_KEEP_LOGS = false;
constexpr std::string_view DEFAULT_LOG_LEVEL = "info";
}  // namespace

bool Config::refresh() {
    // Check if config file exists
    if (!configFileExists()) {
        Logger::get().error()
            << "Config file not found at: " << CONFIG_PATH << endl;
        return false;
    }

    // Load and validate each channel's configuration.
    // Each channel owns its enable state: toggle → load → validate.
    // If any step fails, the channel is disabled and its config strings are
    // freed.
    auto loadChannel = [&](auto& channel, const char* toggleKey) {
        channel.enabled =
            IniHelpers::getBool("general", toggleKey, false, CONFIG_PATH);
        if (channel.enabled) {
            channel.load(CONFIG_PATH);
            if (!channel.validate()) {
                channel = {};  // reset to defaults, frees strings
            }
        }
    };

// Load each channel via the unified list (channels/channels.inc)
#define CHANNEL(Ns, M) loadChannel(M, #M);
#include "channels/channels.inc"
#undef CHANNEL

    // Read general settings
    m_keepLogs = IniHelpers::getBool("general", "keep_logs", DEFAULT_KEEP_LOGS,
                                     CONFIG_PATH);
    m_logLevel = IniHelpers::getString("general", "log_level",
                                       DEFAULT_LOG_LEVEL, CONFIG_PATH);

    // Validate log level
    if (m_logLevel != "debug" && m_logLevel != "info" && m_logLevel != "warn" &&
        m_logLevel != "error") {
        Logger::get().warn()
            << "Invalid log_level: '" << m_logLevel
            << "' (valid levels: debug, info, warn, error). Resetting to "
               "default (info)."
            << endl;
        m_logLevel = std::string(DEFAULT_LOG_LEVEL);
    }

    // Read check interval with minimum enforcement
    m_checkIntervalSeconds =
        std::max(static_cast<int>(IniHelpers::getLong(
                     "general", "check_interval",
                     DEFAULT_CHECK_INTERVAL_SECONDS, CONFIG_PATH)),
                 CHECK_INTERVAL_MINIMUM);

    // Check if at least one channel is enabled
    {
        bool anyEnabled = false;
#define CHANNEL(Ns, M) \
    if (M.enabled) anyEnabled = true;
#include "channels/channels.inc"
#undef CHANNEL
        if (!anyEnabled) return false;
    }

    return true;
}
