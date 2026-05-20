#pragma once

#include <minIni.h>

#include <string>
#include <string_view>

namespace IniHelpers {

/**
 * Read a string value from INI file
 */
inline std::string getString(const char* section, const char* key,
                             std::string_view default_value,
                             const char* configPath) {
    char stackBuffer[256];
    int len = ini_gets(section, key, default_value.data(), stackBuffer,
                       sizeof(stackBuffer), configPath);
    if (len > 0) {
        return std::string(stackBuffer, len);
    }
    return std::string(default_value);
}

/**
 * Read a boolean value from INI file
 */
inline bool getBool(const char* section, const char* key, bool default_value,
                    const char* configPath) {
    return ini_getbool(section, key, default_value ? 1 : 0, configPath) != 0;
}

/**
 * Read a long integer value from INI file
 */
inline long getLong(const char* section, const char* key, long default_value,
                    const char* configPath) {
    return ini_getl(section, key, default_value, configPath);
}

}  // namespace IniHelpers
