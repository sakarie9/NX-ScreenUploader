#pragma once
#include <switch.h>

#include <string_view>

[[nodiscard]] bool sendFileToServer(std::string_view path, size_t size,
                                    bool compression);
