#include <curl/curl.h>
#include <dirent.h>
#include <switch.h>

#include <cstring>
#include <string_view>

#include "album.hpp"
#include "config.hpp"
#include "logger.hpp"
#include "project.h"
#include "queue.hpp"
#include "upload.hpp"
#include "utils.hpp"

namespace {
// Reduce heap size for memory optimization
constexpr size_t INNER_HEAP_SIZE = 0x50000;  // 320KB

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

void NX_NORETURN __nx_exit(Result rc, LoaderReturnFn retaddr);

void __libnx_init_time(void);

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

    // Initialise the time
    rc = timeInitialize();
    __libnx_init_time();
    // exit time
    timeExit();

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

    // Set log level from configuration
    std::string_view logLevelStr = Config::get().getLogLevel();
    if (logLevelStr == "debug") {
        Logger::get().setLevel(LogLevel::DEBUG);
    } else if (logLevelStr == "info") {
        Logger::get().setLevel(LogLevel::INFO);
    } else if (logLevelStr == "warn") {
        Logger::get().setLevel(LogLevel::WARN);
    } else if (logLevelStr == "error") {
        Logger::get().setLevel(LogLevel::ERROR);
    }

    constexpr std::string_view separator = "=============================";
    auto logger = Logger::get().none();
    logger << separator << endl;
    logger << APP_TITLE " v" << APP_VERSION << " is starting..." << endl;
    logger << separator << endl;
}

// Exponential backoff delay helper (1s, 2s, 4s...)
inline void exponentialBackoff(int retryCount) {
    const u64 delayMs = (1ULL << retryCount) * 1000ULL;
    svcSleepThread(delayMs * 1'000'000ULL);
}

// Process upload queue
void processUploadQueue() {
    // Process all tasks in queue until empty
    while (true) {
        char filePath[128];

        // Try to get a task from the queue
        if (!queueGet(filePath, sizeof(filePath))) {
            break;  // Queue empty, exit
        }

        // Determine max retries based on file type
        const bool isVideo = isVideoFile(filePath);
        const int maxRetries = getMaxRetries(isVideo);

        Logger::get().info() << "Uploading: " << filePath << " ("
                             << (isVideo ? "video" : "image") << ", max "
                             << maxRetries << " retries)" << endl;

        bool anySuccess = false;

        // Retry helper: upload via a channel with exponential backoff
        auto tryUpload = [&](const char* name, bool enabled, auto send) {
            if (!enabled) return;
            bool sent = false;
            for (int retry = 0; retry < maxRetries && !sent; ++retry) {
                if (retry > 0) {
                    Logger::get().info() << "[" << name << "] Retry " << retry
                                         << "/" << maxRetries << endl;
                    exponentialBackoff(retry - 1);
                }
                sent = send();
            }
            if (sent)
                anySuccess = true;
            else
                Logger::get().error() << "[" << name << "] Upload failed after "
                                      << maxRetries << " attempts" << endl;
        };

// Upload via each channel (defined in channels/channels.inc)
#define CHANNEL(Ns, M)                      \
    tryUpload(#Ns, Config::get().M.enabled, \
              [&] { return Ns##Channel::send(filePath); });
#include "channels/channels.inc"
#undef CHANNEL

        if (!anySuccess) {
            Logger::get().error() << "All uploads failed" << endl;
        }
    }
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
               "available (Telegram, Ntfy, Discord and Immich are disabled or "
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
#define CHANNEL(Ns, M) \
    if (Config::get().M.enabled) logger << "[" #Ns "] ";
#include "channels/channels.inc"
#undef CHANNEL
        logger << endl;
    }

    // Get check interval configuration
    const int checkInterval = Config::get().getCheckIntervalSeconds();
    const u64 sleepDuration =
        static_cast<u64>(checkInterval) * 1'000'000'000ULL;
    Logger::get().info() << "Check interval: " << checkInterval << " second(s)"
                         << endl;

    // Initialize queue
    queueInit();

    // Main detection loop (runs forever for sysmodule)
    while (true) {
        Logger::get().debug() << "Loop iteration" << endl;

        // Get the last known item path for comparison
        // Use string instead of std::string_view to prevent dangling pointer
        const std::string lastItemPath =
            lastItemResult.has_value() ? lastItemResult.value() : "";

        auto newItemsResult = getNewAlbumItems(lastItemPath);

        // Skip if error (album not ready)
        if (!newItemsResult.has_value()) {
            svcSleepThread(sleepDuration);
            continue;
        }

        const auto& newItems = newItemsResult.value();

        // Process all new items
        for (const auto& item : newItems) {
            if (filesize(item) > 0) {
                if (queueAdd(item.c_str())) {
                    Logger::get().info()
                        << "New: " << item << " (queue: " << queueCount() << ")"
                        << endl;

                    // Update lastItemResult only after successful queue
                    // addition
                    lastItemResult = item;
                } else {
                    Logger::get().error()
                        << "Queue full, skipping: " << item << endl;
                    // Do not update lastItemResult - we'll retry this item on
                    // next iteration
                }
            }
        }

        // Process the upload queue immediately after detecting new items
        if (queueCount() > 0) {
            processUploadQueue();
        }

        svcSleepThread(sleepDuration);
    }
}
