#pragma once

#include <string_view>

/**
 * Upload mode constants - stored as strings to avoid unnecessary conversions
 * Use these constants directly instead of enums
 */
namespace UploadMode {
constexpr std::string_view Compressed = "compressed";
constexpr std::string_view Original = "original";
constexpr std::string_view Both = "both";
}  // namespace UploadMode

/**
 * Configuration default values
 * This is the single source of truth for all default configuration values
 */
namespace ConfigDefaults {

// ============================================================================
// General settings
// ============================================================================
constexpr int CHECK_INTERVAL_SECONDS = 5;
constexpr int CHECK_INTERVAL_MINIMUM = 1;
constexpr bool KEEP_LOGS = false;
constexpr std::string_view LOG_LEVEL = "info";  // debug, info, warn, error

// ============================================================================
// Upload destination toggles
// ============================================================================
constexpr bool TELEGRAM_ENABLED = false;
constexpr bool NTFY_ENABLED = false;
constexpr bool DISCORD_ENABLED = false;

// ============================================================================
// Telegram configuration
// ============================================================================
constexpr std::string_view TELEGRAM_BOT_TOKEN = "";
constexpr std::string_view TELEGRAM_CHAT_ID = "";
constexpr std::string_view TELEGRAM_API_URL = "https://api.telegram.org";
constexpr bool TELEGRAM_UPLOAD_SCREENSHOTS = true;
constexpr bool TELEGRAM_UPLOAD_MOVIES = true;
constexpr std::string_view TELEGRAM_UPLOAD_MODE = UploadMode::Compressed;

// ============================================================================
// Ntfy configuration
// ============================================================================
constexpr std::string_view NTFY_URL = "https://ntfy.sh";
constexpr std::string_view NTFY_TOPIC = "";
constexpr std::string_view NTFY_TOKEN = "";
constexpr std::string_view NTFY_PRIORITY = "default";
constexpr bool NTFY_UPLOAD_SCREENSHOTS = true;
constexpr bool NTFY_UPLOAD_MOVIES = false;

// ============================================================================
// Discord configuration
// ============================================================================
constexpr std::string_view DISCORD_BOT_TOKEN = "";
constexpr std::string_view DISCORD_CHANNEL_ID = "";
constexpr std::string_view DISCORD_API_URL = "https://discord.com/api/v10";
constexpr bool DISCORD_UPLOAD_SCREENSHOTS = true;
constexpr bool DISCORD_UPLOAD_MOVIES = false;

// ============================================================================
// Configuration validation utilities
// ============================================================================

/**
 * Check if upload mode string is valid
 * Returns true if mode is one of the valid modes
 */
constexpr bool isUploadModeValid(std::string_view mode) noexcept {
    return mode == UploadMode::Compressed || mode == UploadMode::Original ||
           mode == UploadMode::Both;
}

/**
 * Check if Telegram configuration is valid
 * Returns true if Telegram is properly configured
 */
constexpr bool isTelegramValid(std::string_view botToken,
                               std::string_view chatId) noexcept {
    // Both token and chat ID must be non-empty
    return !botToken.empty() && !chatId.empty();
}

/**
 * Check if Ntfy configuration is valid
 * Returns true if Ntfy is properly configured
 */
constexpr bool isNtfyValid(std::string_view topic) noexcept {
    // Topic must be non-empty
    return !topic.empty();
}

/**
 * Check if Discord configuration is valid
 * Returns true if Discord is properly configured
 */
constexpr bool isDiscordValid(std::string_view botToken,
                              std::string_view channelId) noexcept {
    // Both token and chat ID must be non-empty
    return !botToken.empty() && !channelId.empty();
}

}  // namespace ConfigDefaults
