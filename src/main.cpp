#include <curl/curl.h>
#include <dirent.h>
#include <switch.h>

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

    // Necessary to get right CapsAlbumStorage after reboot
    rc = nsInitialize();
    if (R_FAILED(rc)) {
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
    nsExit();
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
    auto logger = Logger::get().none();
    logger << separator << endl;
    logger << APP_TITLE " v" << APP_VERSION << " is starting..." << endl;
    logger << separator << endl;
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
    constexpr std::string_view configDir = "sdmc:/config";
    constexpr std::string_view appConfigDir = "sdmc:/config/" APP_TITLE;

    mkdir(configDir.data(), 0700);
    mkdir(appConfigDir.data(), 0700);

    // Initialize logger first (with truncate) before loading config
    // so that config errors are properly logged
    initLogger(true);

    if (!Config::get().refresh()) {
        Logger::get().error()
            << "Configuration validation failed: No valid upload channel "
               "available (Telegram, Ntfy and Discord are disabled or "
               "misconfigured)."
            << endl;
        Logger::get().error() << "Please check your config.ini file and ensure "
                                 "at least one channel is properly configured."
                              << endl;
        Logger::get().close();
        return 0;
    }

    if (!Config::get().keepLogs()) {
        // Truncate logs if not keeping them
        Logger::get().close();
        Logger::get().truncate();
        initLogger(false);
    }

    CapsAlbumStorage storage;
    FsFileSystem imageFs;

    Result rc = capsaGetAutoSavingStorage(&storage);
    if (!R_SUCCEEDED(rc)) {
        Logger::get().error() << "capsaGetAutoSavingStorage() failed: " << rc
                              << ", exiting..." << endl;
        return 0;
    }

    rc = fsOpenImageDirectoryFileSystem(
        &imageFs, static_cast<FsImageDirectoryId>(storage));
    if (!R_SUCCEEDED(rc)) {
        Logger::get().error()
            << "fsOpenImageDirectoryFileSystem() failed: " << rc
            << ", exiting..." << endl;
        return 0;
    }

    const int mountRes = fsdevMountDevice("img", imageFs);
    if (mountRes < 0) {
        Logger::get().error()
            << "fsdevMountDevice() failed, exiting..." << endl;
        return 0;
    }

    Logger::get().info() << "Mounted " << (storage ? "SD" : "NAND")
                         << " storage" << endl;

    // Get the initial last file (for comparison)
    // If album is not ready (Err), we'll use the first valid item later
    auto lastItemResult = getLastAlbumItem();
    if (lastItemResult.has_value()) {
        Logger::get().info()
            << "Current last item: " << lastItemResult.value() << endl;
    } else {
        Logger::get().info()
            << "Album not ready: " << lastItemResult.error() << endl;
    }

    // Log enabled upload channels
    {
        auto logger = Logger::get().info();
        logger << "Enabled upload channels: ";
        if (Config::get().telegramEnabled()) {
            logger << "[Telegram] ";
        }
        if (Config::get().ntfyEnabled()) {
            logger << "[Ntfy]";
        }
        if (Config::get().discordEnabled()) {
            logger << "[Discord]";
        }
        logger << endl;
    }

    // Determine Telegram upload mode based on configuration
    const std::string_view telegramUploadMode =
        Config::get().getTelegramUploadMode();
    if (Config::get().telegramEnabled()) {
        Logger::get().info()
            << "Telegram upload mode: " << telegramUploadMode << endl;
    }

    // Get check interval configuration
    const int checkInterval = Config::get().getCheckIntervalSeconds();
    const u64 sleepDuration =
        static_cast<u64>(checkInterval) * 1'000'000'000ULL;
    Logger::get().info() << "Check interval: " << checkInterval << " second(s)"
                         << endl;
    Logger::get().close();

    constexpr std::string_view separator = "=============================";
    constexpr int maxRetries = 3;

    // Helper lambda to retry upload with max attempts
    const auto retryUpload = [maxRetries]<typename F>(F&& uploadFunc) {
        for (int retry = 0; retry < maxRetries; ++retry) {
            if (uploadFunc()) return true;
        }
        return false;
    };

    while (true) {
        auto tmpItemResult = getLastAlbumItem();

        // Skip if tmpItem is an error (album not ready)
        if (!tmpItemResult.has_value()) {
            svcSleepThread(sleepDuration);
            continue;
        }

        const std::string& tmpItem = tmpItemResult.value();

        // If lastItem was an error, or tmpItem is newer, process it
        const bool shouldProcess =
            !lastItemResult.has_value() || lastItemResult.value() < tmpItem;

        if (shouldProcess) {
            const size_t fs = filesize(tmpItem);

            if (fs > 0) {
                auto logger = Logger::get().info();
                logger << separator << endl
                       << "New item found: " << tmpItem << endl
                       << "Filesize: " << fs << endl;

                bool anySuccess = false;

                // Upload to enabled destinations in sequence
                if (Config::get().telegramEnabled()) {
                    bool sent = false;

                    // Decide upload strategy based on configured mode
                    if (telegramUploadMode == UploadMode::Compressed) {
                        sent = retryUpload([&] {
                            return sendFileToTelegram(tmpItem, fs, true);
                        });
                    } else if (telegramUploadMode == UploadMode::Original) {
                        sent = retryUpload([&] {
                            return sendFileToTelegram(tmpItem, fs, false);
                        });
                    } else if (telegramUploadMode == UploadMode::Both) {
                        // Send compressed first, then original
                        const bool compressedSent = retryUpload([&] {
                            return sendFileToTelegram(tmpItem, fs, true);
                        });
                        const bool originalSent = retryUpload([&] {
                            return sendFileToTelegram(tmpItem, fs, false);
                        });
                        sent = compressedSent || originalSent;
                    }

                    if (!sent) {
                        Logger::get().error()
                            << "[Telegram] Unable to send file after "
                            << maxRetries << " retries" << endl;
                    } else {
                        anySuccess = true;
                    }
                }

                // Upload to ntfy (always original, no compression)
                if (Config::get().ntfyEnabled()) {
                    const bool sent = retryUpload(
                        [&] { return sendFileToNtfy(tmpItem, fs); });

                    if (!sent) {
                        Logger::get().error()
                            << "[ntfy] Unable to send file after " << maxRetries
                            << " retries" << endl;
                    } else {
                        anySuccess = true;
                    }
                }

                // Upload to Discord (always original, no compression)
                if (Config::get().discordEnabled()) {
                    const bool sent = retryUpload(
                        [&] { return sendFileToDiscord(tmpItem, fs); });

                    if (!sent) {
                        Logger::get().error()
                            << "[Discord] Unable to send file after " << maxRetries
                            << " retries" << endl;
                    } else {
                        anySuccess = true;
                    }
                }

                if (!anySuccess) {
                    Logger::get().error()
                        << "All upload destinations failed, skipping..."
                        << endl;
                }

                // Update lastItemResult regardless of success to avoid
                // retrying the same file forever
                lastItemResult = tmpItem;
            }

            Logger::get().close();
        }

        svcSleepThread(sleepDuration);
    }
}
