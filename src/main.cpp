#include <curl/curl.h>
#include <dirent.h>
#include <switch.h>

#include <cstring>
#include <string_view>

#include "album.hpp"
#include "config.hpp"
#include "logger.hpp"
#include "project.h"
#include "upload.hpp"
#include "utils.hpp"

namespace {
// Reduce heap size for memory optimization
constexpr size_t INNER_HEAP_SIZE = 0x60000;  // 344KB

// Upload task with fixed-size buffer (no heap allocation)
struct UploadTask {
    char filePath[128];  // Fixed buffer for path
    size_t fileSize;
    bool valid;
};

// Simple circular queue with static allocation
constexpr size_t MAX_QUEUE_SIZE = 8;
UploadTask g_uploadQueue[MAX_QUEUE_SIZE];
size_t g_queueHead = 0;  // Read position
size_t g_queueTail = 0;  // Write position
size_t g_queueCount = 0;

Mutex g_queueMutex;
Thread g_uploadThread;  // Static thread object
volatile bool g_threadRunning = false;
volatile bool g_threadCreated = false;
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

// Exponential backoff delay helper (1s, 2s, 4s...)
inline void exponentialBackoff(int retryCount) {
    const u64 delayMs = (1ULL << retryCount) * 1000ULL;
    svcSleepThread(delayMs * 1'000'000ULL);
}

// Upload worker thread function
void uploadWorkerThread(void* arg) {
    Logger::get().info() << "[Worker] Started" << endl;

    // Get config values once
    const std::string_view telegramUploadMode =
        Config::get().getTelegramUploadMode();
    const bool telegramEnabled = Config::get().telegramEnabled();
    const bool ntfyEnabled = Config::get().ntfyEnabled();
    const bool discordEnabled = Config::get().discordEnabled();

    // Process all tasks in queue until empty
    while (true) {
        char filePath[128];
        size_t fileSize = 0;
        bool hasTask = false;

        // Try to get a task from the queue
        mutexLock(&g_queueMutex);
        if (g_queueCount > 0) {
            std::memcpy(filePath, g_uploadQueue[g_queueHead].filePath,
                        sizeof(filePath));
            fileSize = g_uploadQueue[g_queueHead].fileSize;
            g_queueHead = (g_queueHead + 1) % MAX_QUEUE_SIZE;
            g_queueCount--;
            hasTask = true;
        }
        mutexUnlock(&g_queueMutex);

        if (!hasTask) {
            break;  // Queue empty, exit thread
        }

        // Determine max retries based on file type (image=2, video=3)
        const int maxRetries = getMaxRetries(filePath);
        const bool isVideo =
            (std::strlen(filePath) >= 4 &&
             std::strcmp(filePath + std::strlen(filePath) - 4, ".mp4") == 0);

        Logger::get().info()
            << "[Worker] Uploading: " << filePath << " (" << fileSize
            << " bytes, " << (isVideo ? "video" : "image") << ", max "
            << maxRetries << " retries)" << endl;

        bool anySuccess = false;

        // Upload to Telegram with exponential backoff
        if (telegramEnabled) {
            bool sent = false;
            for (int retry = 0; retry < maxRetries && !sent; ++retry) {
                if (retry > 0) {
                    Logger::get().info() << "[Telegram] Retry " << retry << "/"
                                         << maxRetries << endl;
                    exponentialBackoff(retry - 1);
                }
                if (telegramUploadMode == UploadMode::Compressed) {
                    sent = sendFileToTelegram(filePath, fileSize, true);
                } else if (telegramUploadMode == UploadMode::Original) {
                    sent = sendFileToTelegram(filePath, fileSize, false);
                } else if (telegramUploadMode == UploadMode::Both) {
                    bool c = sendFileToTelegram(filePath, fileSize, true);
                    bool o = sendFileToTelegram(filePath, fileSize, false);
                    sent = c || o;
                }
            }
            if (sent)
                anySuccess = true;
            else
                Logger::get().error() << "[Telegram] Upload failed after "
                                      << maxRetries << " attempts" << endl;
        }

        // Upload to ntfy with exponential backoff
        if (ntfyEnabled) {
            bool sent = false;
            for (int retry = 0; retry < maxRetries && !sent; ++retry) {
                if (retry > 0) {
                    Logger::get().info() << "[ntfy] Retry " << retry << "/"
                                         << maxRetries << endl;
                    exponentialBackoff(retry - 1);
                }
                sent = sendFileToNtfy(filePath, fileSize);
            }
            if (sent)
                anySuccess = true;
            else
                Logger::get().error() << "[ntfy] Upload failed after "
                                      << maxRetries << " attempts" << endl;
        }

        // Upload to Discord with exponential backoff
        if (discordEnabled) {
            bool sent = false;
            for (int retry = 0; retry < maxRetries && !sent; ++retry) {
                if (retry > 0) {
                    Logger::get().info() << "[Discord] Retry " << retry << "/"
                                         << maxRetries << endl;
                    exponentialBackoff(retry - 1);
                }
                sent = sendFileToDiscord(filePath, fileSize);
            }
            if (sent)
                anySuccess = true;
            else
                Logger::get().error() << "[Discord] Upload failed after "
                                      << maxRetries << " attempts" << endl;
        }

        if (!anySuccess) {
            Logger::get().error() << "All uploads failed" << endl;
        }
    }

    g_threadRunning = false;
    Logger::get().info() << "[Worker] Exiting" << endl;
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

    // Initialize mutex
    mutexInit(&g_queueMutex);

    // Main detection loop (runs forever for sysmodule)
    while (true) {
        // Get the last known item path for comparison
        std::string_view lastItemPath =
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
            const size_t fs = filesize(item);

            if (fs > 0) {
                bool added = false;
                size_t queueSize = 0;

                // Add file to upload queue (circular buffer)
                mutexLock(&g_queueMutex);
                if (g_queueCount < MAX_QUEUE_SIZE) {
                    std::strncpy(g_uploadQueue[g_queueTail].filePath,
                                 item.c_str(), 127);
                    g_uploadQueue[g_queueTail].filePath[127] = '\0';
                    g_uploadQueue[g_queueTail].fileSize = fs;
                    g_queueTail = (g_queueTail + 1) % MAX_QUEUE_SIZE;
                    g_queueCount++;
                    queueSize = g_queueCount;
                    added = true;
                }
                mutexUnlock(&g_queueMutex);

                if (added) {
                    Logger::get().info()
                        << "New: " << item << " (queue: " << queueSize << ")"
                        << endl;
                } else {
                    Logger::get().error()
                        << "Queue full, skipping: " << item << endl;
                }

                // Update lastItemResult to track the newest processed item
                lastItemResult = item;

                // Start upload thread only if not already running and queue has
                // items
                if (!g_threadRunning && queueSize > 0) {
                    // Wait for previous thread to fully exit if needed
                    if (g_threadCreated) {
                        threadWaitForExit(&g_uploadThread);
                        threadClose(&g_uploadThread);
                        g_threadCreated = false;
                    }

                    // Create thread with minimal stack (16KB)
                    Result rc2 =
                        threadCreate(&g_uploadThread, uploadWorkerThread,
                                     nullptr, nullptr, 0x4000, 0x2C, -2);
                    if (R_FAILED(rc2)) {
                        Logger::get().error()
                            << "Thread create failed: " << rc2 << endl;
                    } else {
                        rc2 = threadStart(&g_uploadThread);
                        if (R_FAILED(rc2)) {
                            Logger::get().error()
                                << "Thread start failed: " << rc2 << endl;
                            threadClose(&g_uploadThread);
                        } else {
                            g_threadCreated = true;
                            g_threadRunning = true;
                        }
                    }
                }
            }
        }

        svcSleepThread(sleepDuration);
    }

    // Cleanup: wait for upload thread if still running
    if (g_threadCreated) {
        threadWaitForExit(&g_uploadThread);
        threadClose(&g_uploadThread);
    }
}
