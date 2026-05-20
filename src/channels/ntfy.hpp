#pragma once

#include <string>
#include <string_view>

namespace NtfyChannel {

// Default values for ntfy channel configuration
namespace Defaults {
constexpr std::string_view URL = "https://ntfy.sh";
constexpr std::string_view TOPIC = "";
constexpr std::string_view TOKEN = "";
constexpr std::string_view PRIORITY = "default";
constexpr bool UPLOAD_SCREENSHOTS = true;
constexpr bool UPLOAD_MOVIES = false;
}  // namespace Defaults

// INI section name
inline constexpr const char* SECTION = "ntfy";

// ntfy channel configuration
struct Config {
    bool enabled{false};

    std::string url{Defaults::URL};
    std::string topic{Defaults::TOPIC};
    std::string token{Defaults::TOKEN};
    std::string priority{Defaults::PRIORITY};
    bool uploadScreenshots{Defaults::UPLOAD_SCREENSHOTS};
    bool uploadMovies{Defaults::UPLOAD_MOVIES};

    /**
     * Load configuration from INI file
     * @param configPath Path to the config.ini file
     * @return true on success
     */
    bool load(const char* configPath);

    /**
     * Check if the configuration is valid.
     * Logs a warning if invalid.
     * @return true if configuration is valid
     */
    bool validate();
};

/// Upload a file to ntfy
[[nodiscard]] bool send(std::string_view path);

}  // namespace NtfyChannel
