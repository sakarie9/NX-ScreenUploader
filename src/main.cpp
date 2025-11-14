#include <curl/curl.h>
#include <dirent.h>
#include <switch.h>

#include <iostream>
#include <string_view>

#include "config.hpp"
#include "logger.hpp"
#include "project.h"
#include "upload.hpp"
#include "utils.hpp"

namespace {
// Reduce heap size for memory optimization
// 0x40000 (256KB) will oom, 0x50000 (320KB) is minimum stable
constexpr size_t INNER_HEAP_SIZE = 0x50000;
constexpr size_t TCP_TX_BUF_SIZE = 0x800;
constexpr size_t TCP_RX_BUF_SIZE = 0x1000;
constexpr size_t TCP_TX_BUF_SIZE_MAX = 0x2EE0;
constexpr size_t TCP_RX_BUF_SIZE_MAX = 0x2EE0;
constexpr size_t UDP_TX_BUF_SIZE = 0;
constexpr size_t UDP_RX_BUF_SIZE = 0;
constexpr size_t SB_EFFICIENCY = 4;
}  // namespace

extern "C" {

extern u32 __start__;

// Sysmodules should not use applet*.
u32 __nx_applet_type = AppletType_None;

// Minimize fs resource usage for memory optimization
u32 __nx_fs_num_sessions = 1;

// Newlib heap configuration function (makes malloc/free work).
void __libnx_initheap(void) {
    static char g_innerheap[INNER_HEAP_SIZE];

    extern char* fake_heap_start;
    extern char* fake_heap_end;

    fake_heap_start = &g_innerheap[0];
    fake_heap_end = &g_innerheap[sizeof g_innerheap];
}

void __appInit(void) {
    Result rc;

    rc = smInitialize();
    if (R_FAILED(rc)) {
        fatalThrow(rc);
    }

    rc = setsysInitialize();
    if (R_SUCCEEDED(rc)) {
        SetSysFirmwareVersion fw;
        rc = setsysGetFirmwareVersion(&fw);
        if (R_SUCCEEDED(rc)) {
            hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
        } else {
            fatalThrow(rc);
        }
        setsysExit();
    } else {
        fatalThrow(rc);
    }

    constexpr SocketInitConfig socket_config = {
        .tcp_tx_buf_size = TCP_TX_BUF_SIZE,
        .tcp_rx_buf_size = TCP_RX_BUF_SIZE,
        .tcp_tx_buf_max_size = TCP_TX_BUF_SIZE_MAX,
        .tcp_rx_buf_max_size = TCP_RX_BUF_SIZE_MAX,

        .udp_tx_buf_size = UDP_TX_BUF_SIZE,
        .udp_rx_buf_size = UDP_RX_BUF_SIZE,

        .sb_efficiency = SB_EFFICIENCY,
    };

    rc = socketInitialize(&socket_config);
    if (R_FAILED(rc)) {
        fatalThrow(rc);
    }

    rc = capsaInitialize();
    if (R_FAILED(rc)) {
        fatalThrow(rc);
    }

    rc = fsInitialize();
    if (R_FAILED(rc)) {
        fatalThrow(rc);
    }

    fsdevMountSdmc();

    curl_global_init(CURL_GLOBAL_DEFAULT);
}

void __appExit(void) {
    curl_global_cleanup();
    fsdevUnmountAll();
    fsExit();
    capsaExit();
    socketExit();
    smExit();
}
}

void initLogger(bool truncate) {
    if (truncate) {
        Logger::get().truncate();
    }

    // Logger::get().setLevel(LogLevel::DEBUG);

    constexpr std::string_view separator = "=============================";
    auto logger = Logger::get().info();
    logger << separator << std::endl;
    logger << APP_TITLE " v" << APP_VERSION << " is starting..." << std::endl;
    logger << separator << std::endl;
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
    constexpr std::string_view configDir = "sdmc:/config";
    constexpr std::string_view appConfigDir = "sdmc:/config/" APP_TITLE;

    mkdir(configDir.data(), 0700);
    mkdir(appConfigDir.data(), 0700);

    initLogger(false);
    if (!Config::get().refresh()) {
        return 0;
    }

    if (!Config::get().keepLogs()) {
        initLogger(true);
    }

    CapsAlbumStorage storage;
    FsFileSystem imageFs;

    Result rc = capsaGetAutoSavingStorage(&storage);
    if (!R_SUCCEEDED(rc)) {
        Logger::get().error() << "capsaGetAutoSavingStorage() failed: " << rc
                              << ", exiting..." << std::endl;
        return 0;
    }

    rc = fsOpenImageDirectoryFileSystem(
        &imageFs, static_cast<FsImageDirectoryId>(storage));
    if (!R_SUCCEEDED(rc)) {
        Logger::get().error()
            << "fsOpenImageDirectoryFileSystem() failed: " << rc
            << ", exiting..." << std::endl;
        return 0;
    }

    const int mountRes = fsdevMountDevice("img", imageFs);
    if (mountRes < 0) {
        Logger::get().error()
            << "fsdevMountDevice() failed, exiting..." << std::endl;
        return 0;
    }

    Logger::get().info() << "Mounted " << (storage ? "SD" : "NAND")
                         << " storage" << std::endl;

    // Get the initial last file (for comparison)
    std::string lastItem = getLastAlbumItem();
    Logger::get().info() << "Current last item: " << lastItem << std::endl;

    // Determine Telegram upload mode based on configuration
    const UploadMode telegramUploadMode = Config::get().getTelegramUploadMode();
    const char* modeStr =
        telegramUploadMode == UploadMode::Compressed ? "compressed"
        : telegramUploadMode == UploadMode::Original ? "original"
                                                     : "both";
    Logger::get().info() << "Telegram upload mode: " << modeStr << std::endl;

    // Get check interval configuration
    const int checkInterval = Config::get().getCheckIntervalSeconds();
    const u64 sleepDuration =
        static_cast<u64>(checkInterval) * 1'000'000'000ULL;
    Logger::get().info() << "Check interval: " << checkInterval << " second(s)"
                         << std::endl;
    Logger::get().close();

    constexpr std::string_view separator = "=============================";
    constexpr int maxRetries = 3;

    while (true) {
        std::string tmpItem = getLastAlbumItem();

        if (lastItem < tmpItem) {
            const size_t fs = filesize(tmpItem);

            if (fs > 0) {
                auto logger = Logger::get().info();
                logger << separator << std::endl
                       << "New item found: " << tmpItem << std::endl
                       << "Filesize: " << fs << std::endl;

                bool anySuccess = false;

                // Upload to enabled destinations in sequence
                if (Config::get().telegramEnabled()) {
                    bool sent = false;

                    // Decide upload strategy based on configured mode
                    switch (telegramUploadMode) {
                        case UploadMode::Compressed:
                            // Try compressed upload only
                            for (int retry = 0; retry < maxRetries && !sent;
                                 ++retry) {
                                sent = sendFileToTelegram(tmpItem, fs, true);
                            }
                            break;

                        case UploadMode::Original:
                            // Try original upload only
                            for (int retry = 0; retry < maxRetries && !sent;
                                 ++retry) {
                                sent = sendFileToTelegram(tmpItem, fs, false);
                            }
                            break;

                        case UploadMode::Both:
                            // Send compressed first, then original
                            bool compressedSent = false;
                            for (int retry = 0;
                                 retry < maxRetries && !compressedSent;
                                 ++retry) {
                                compressedSent =
                                    sendFileToTelegram(tmpItem, fs, true);
                            }

                            bool originalSent = false;
                            for (int retry = 0;
                                 retry < maxRetries && !originalSent; ++retry) {
                                originalSent =
                                    sendFileToTelegram(tmpItem, fs, false);
                            }

                            sent = compressedSent || originalSent;
                            break;
                    }

                    if (!sent) {
                        Logger::get().error()
                            << "[Telegram] Unable to send file after "
                            << maxRetries << " retries" << std::endl;
                    } else {
                        anySuccess = true;
                    }
                }

                // Upload to ntfy (always original, no compression)
                if (Config::get().ntfyEnabled()) {
                    bool sent = false;

                    for (int retry = 0; retry < maxRetries && !sent; ++retry) {
                        sent = sendFileToNtfy(tmpItem, fs);
                    }

                    if (!sent) {
                        Logger::get().error()
                            << "[ntfy] Unable to send file after " << maxRetries
                            << " retries" << std::endl;
                    } else {
                        anySuccess = true;
                    }
                }

                if (!anySuccess) {
                    Logger::get().error()
                        << "All upload destinations failed, skipping..."
                        << std::endl;
                }

                // Update lastItem regardless of success to avoid retrying the
                // same file forever
                lastItem = std::move(tmpItem);
            }

            Logger::get().close();
        }

        svcSleepThread(sleepDuration);
    }
}
