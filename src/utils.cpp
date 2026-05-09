#include "utils.hpp"

#include <sys/stat.h>

#include <ctime>
#include <string>
#include <string_view>

#include "logger.hpp"

size_t filesize(std::string_view path) {
    struct stat st;
    if (stat(path.data(), &st) != 0) return 0;
    return static_cast<size_t>(st.st_size);
}

std::string url_encode(std::string_view value) {
    std::string result;
    // More conservative reservation - typically most chars don't need encoding
    result.reserve(value.size() + (value.size() / 4));

    constexpr std::string_view hexChars = "0123456789ABCDEF";

    // constexpr-friendly character check
    constexpr auto isUnreserved = [](unsigned char c) constexpr noexcept {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
               (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
               c == '~';
    };

    for (const unsigned char c : value) {
        if (isUnreserved(c)) {
            result.push_back(static_cast<char>(c));
        } else {
            result.push_back('%');
            result.push_back(hexChars[c >> 4]);
            result.push_back(hexChars[c & 0x0F]);
        }
    }

    return result;
}

namespace {

time_t currentTime() noexcept {
    time_t t;
    time(&t);
    return t;
}

}  // namespace

std::string getCurrentTimeISO8601() {
    const time_t t = currentTime();
    char buf[sizeof "2026-05-09T12:00:00Z"];
    strftime(buf, sizeof buf, "%FT%TZ", gmtime(&t));
    return std::string(buf);
}

std::string getCurrentTimeFormatted(const char* fmt) {
    const time_t t = currentTime();
    char buf[64];
    strftime(buf, sizeof buf, fmt, localtime(&t));
    return std::string(buf);
}

std::string getCurrentTimeLocal() {
    return getCurrentTimeFormatted("%Y-%m-%d %H:%M:%S");
}