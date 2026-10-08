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

// Accepted ranges for the upload tuning values. Anything outside the range
// is clamped so that a typo cannot make uploads hang forever.
constexpr long CONNECT_TIMEOUT_MIN_S = 1;
constexpr long CONNECT_TIMEOUT_MAX_S = 60;
constexpr long IDLE_TIMEOUT_MIN_S = 5;
constexpr long IDLE_TIMEOUT_MAX_S = 600;
constexpr long TOTAL_TIMEOUT_MIN_S = 10;
constexpr long TOTAL_TIMEOUT_MAX_S = 3600;
constexpr long LOW_SPEED_LIMIT_MIN_BPS = 1;
constexpr long LOW_SPEED_LIMIT_MAX_BPS = 1048576;
constexpr long MAX_ATTEMPTS_MIN = 1;
constexpr long MAX_ATTEMPTS_MAX = 10;
constexpr long RETRY_DELAY_MIN_MS = 100;
constexpr long RETRY_DELAY_MAX_MS = 120000;
constexpr long RETRY_AFTER_MIN_MS = 1000;
constexpr long RETRY_AFTER_MAX_MS = 300000;

// Read a numeric value from [general] and clamp it to the given range
long readClampedLong(const char* key, long defaultValue, long minimum,
                     long maximum) {
    const long value =
        IniHelpers::getLong("general", key, defaultValue, CONFIG_PATH);
    const long clamped = std::clamp(value, minimum, maximum);

    if (clamped != value) {
        Logger::get().warn() << "Invalid " << key << ": " << value
                             << " (valid range: " << minimum << ".." << maximum
                             << "). Using " << clamped << "." << endl;
    }
    return clamped;
}
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

    // Read PNGShot compatibility mode
    m_pngshotEnabled =
        IniHelpers::getBool("general", "pngshot", false, CONFIG_PATH);

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

    // Read upload timeouts and retry policy per file type. Both policies
    // start out with the built-in defaults, so the current value is used as
    // the fallback for every key.
    auto loadPolicy = [&](UploadPolicy& policy, const char* type) {
        const std::string prefix = std::string("upload_") + type + "_";
        const auto key = [&](const char* name) { return prefix + name; };

        policy.connectTimeout =
            readClampedLong(key("connect_timeout").c_str(),
                            policy.connectTimeout, CONNECT_TIMEOUT_MIN_S,
                            CONNECT_TIMEOUT_MAX_S);
        policy.idleTimeout = readClampedLong(key("idle_timeout").c_str(),
                                             policy.idleTimeout,
                                             IDLE_TIMEOUT_MIN_S,
                                             IDLE_TIMEOUT_MAX_S);
        policy.totalTimeout = readClampedLong(key("total_timeout").c_str(),
                                              policy.totalTimeout,
                                              TOTAL_TIMEOUT_MIN_S,
                                              TOTAL_TIMEOUT_MAX_S);
        policy.maxAttempts = static_cast<int>(readClampedLong(
            key("max_attempts").c_str(), policy.maxAttempts, MAX_ATTEMPTS_MIN,
            MAX_ATTEMPTS_MAX));

        // A total timeout below the connect timeout would abort every
        // transfer before the connection is even established.
        if (policy.totalTimeout < policy.connectTimeout) {
            Logger::get().warn()
                << "upload_" << type << "_total_timeout ("
                << policy.totalTimeout << "s) is shorter than the connect "
                << "timeout (" << policy.connectTimeout << "s). Raising it."
                << endl;
            policy.totalTimeout = policy.connectTimeout;
        }
    };

    loadPolicy(m_imagePolicy, "image");
    loadPolicy(m_videoPolicy, "video");

    // Shared tuning: read once, then applied to both policies
    const long lowSpeedLimit =
        readClampedLong("upload_low_speed_limit", m_imagePolicy.lowSpeedLimit,
                        LOW_SPEED_LIMIT_MIN_BPS, LOW_SPEED_LIMIT_MAX_BPS);
    const long retryBaseDelayMs = readClampedLong(
        "upload_retry_base_ms", m_imagePolicy.retryBaseDelayMs,
        RETRY_DELAY_MIN_MS, RETRY_DELAY_MAX_MS);
    long retryMaxDelayMs =
        readClampedLong("upload_retry_max_ms", m_imagePolicy.retryMaxDelayMs,
                        RETRY_DELAY_MIN_MS, RETRY_DELAY_MAX_MS);
    const long retryAfterMaxMs = readClampedLong(
        "upload_retry_after_cap_ms", m_imagePolicy.retryAfterMaxMs,
        RETRY_AFTER_MIN_MS, RETRY_AFTER_MAX_MS);

    if (retryMaxDelayMs < retryBaseDelayMs) {
        Logger::get().warn()
            << "upload_retry_max_ms (" << retryMaxDelayMs
            << "ms) is below upload_retry_base_ms (" << retryBaseDelayMs
            << "ms). Raising it." << endl;
        retryMaxDelayMs = retryBaseDelayMs;
    }

    for (UploadPolicy* policy : {&m_imagePolicy, &m_videoPolicy}) {
        policy->lowSpeedLimit = lowSpeedLimit;
        policy->retryBaseDelayMs = retryBaseDelayMs;
        policy->retryMaxDelayMs = retryMaxDelayMs;
        policy->retryAfterMaxMs = retryAfterMaxMs;
    }

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
