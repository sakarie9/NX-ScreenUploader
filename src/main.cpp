#include <curl/curl.h>
#include <dirent.h>
#include <switch.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

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

// Stub out C++ exception handling to save RAM.
// Since -fno-exceptions is used, these should never be called,
// but the runtime still links in unwind table data. Wrapping them
// allows --gc-sections to strip the unused unwind code.
// Credits to MasaGratoR for the approach.
void __wrap___cxa_throw(void *thrown_exception, void *pvar, void (*dest)(void *)) {
    abort();
}

void __wrap__Unwind_Resume() {
    // no-op
}

void __wrap___gxx_personality_v0() {
    // no-op
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

// Delay before the next upload attempt. A server provided Retry-After value
// wins, otherwise the delay grows exponentially and gets a bounded random
// jitter so that several channels do not retry in lockstep.
inline u64 retryDelayMs(const UploadPolicy& policy, int attempt,
                        long retryAfterSec) {
    if (retryAfterSec > 0) {
        const long seconds =
            std::min(retryAfterSec, policy.retryAfterMaxMs / 1000);
        return static_cast<u64>(seconds * 1000L);
    }

    const long shift = std::min(attempt - 1, 4);
    const long base =
        std::min(policy.retryBaseDelayMs << shift, policy.retryMaxDelayMs);

    // +/- 20% jitter
    const long jitter = base / 5 + 1;
    const long delay = base - jitter / 2 + (rand() % jitter);
    return static_cast<u64>(delay);
}

// Short human readable description of a failed upload attempt
inline std::string describeFailure(const UploadOutcome& outcome) {
    std::string description = toString(outcome.status);
    if (outcome.httpCode != 0) {
        description += " (HTTP " + std::to_string(outcome.httpCode) + ")";
    }
    return description;
}

// Milliseconds elapsed since the given system tick
inline u64 elapsedMs(u64 startTick) {
    const u64 freq = armGetSystemTickFreq();
    if (freq == 0) {
        return 0;
    }
    return (armGetSystemTick() - startTick) * 1000ULL / freq;
}

// Convert a duration in milliseconds to system ticks
inline u64 msToTicks(u64 ms) {
    const u64 freq = armGetSystemTickFreq();
    if (freq == 0) {
        return 0;
    }
    return ms * freq / 1000ULL;
}

// Number of upload channels, derived from the unified channel list
inline constexpr int CHANNEL_COUNT = 0
#define CHANNEL(Ns, M) +1
#include "channels/channels.inc"
#undef CHANNEL
    ;

static_assert(CHANNEL_COUNT <= 8, "Done/success masks are 8 bits wide");

// Bit mask of the channels that are enabled in the configuration
uint8_t enabledChannelMask() {
    uint8_t mask = 0;
    int index = 0;
#define CHANNEL(Ns, M)                               \
    if (Config::get().M.enabled) {                   \
        mask |= static_cast<uint8_t>(1u << index);   \
    }                                                \
    ++index;
#include "channels/channels.inc"
#undef CHANNEL
    return mask;
}

// Run one upload round for every task that is due. A round attempts each
// channel that still needs to upload exactly once; tasks that are not finished
// afterwards are scheduled for a later round instead of blocking the detection
// loop with a long backoff sleep.
void processDueUploads() {
    const uint8_t enabledMask = enabledChannelMask();

    DueUpload task;
    while (queueTakeDue(armGetSystemTick(), task)) {
        // Timeouts, retry budget and attempt count depend on the file type
        const bool isVideo = isVideoFile(task.filePath);
        const UploadPolicy& policy = Config::get().policy(isVideo);
        const int maxAttempts = policy.maxAttempts;
        const int attempt = task.round + 1;

        // Optional hard limit for the whole file, counted over all channels
        // and rounds. It starts with the first attempt, not with the upload.
        const u64 budgetMs = static_cast<u64>(policy.itemBudgetS) * 1000ULL;
        u64 firstAttemptTick = task.firstAttemptTick;
        if (firstAttemptTick == 0) {
            firstAttemptTick = armGetSystemTick();
        }

        Logger::get().info() << "Uploading: " << task.filePath << " ("
                             << (isVideo ? "video" : "image") << ", attempt "
                             << attempt << "/" << maxAttempts << ")" << endl;

        uint8_t doneMask = task.doneMask;
        uint8_t successMask = task.successMask;
        long retryAfterSec = 0;
        bool budgetExhausted = false;
        int index = 0;

// Upload via each channel that still needs an attempt (channels/channels.inc)
#define CHANNEL(Ns, M)                                                  \
    do {                                                                \
        const uint8_t bit = static_cast<uint8_t>(1u << index);          \
        ++index;                                                        \
        if (budgetExhausted || (enabledMask & bit) == 0 ||              \
            (doneMask & bit) != 0) {                                    \
            break;                                                      \
        }                                                               \
        if (budgetMs != 0 && elapsedMs(firstAttemptTick) >= budgetMs) { \
            budgetExhausted = true;                                     \
            break;                                                      \
        }                                                               \
        const UploadOutcome outcome = Ns##Channel::send(task.filePath); \
        if (outcome.ok()) {                                             \
            doneMask |= bit;                                            \
            successMask |= bit;                                         \
        } else if (outcome.status == UploadStatus::Permanent) {         \
            doneMask |= bit;                                            \
            Logger::get().error()                                       \
                << "[" #Ns "] " << describeFailure(outcome)             \
                << ", giving up" << endl;                               \
        } else {                                                        \
            retryAfterSec =                                             \
                std::max(retryAfterSec, outcome.retryAfterSec);         \
            Logger::get().warn()                                        \
                << "[" #Ns "] " << describeFailure(outcome)             \
                << ", will retry" << endl;                              \
        }                                                               \
    } while (false);
#include "channels/channels.inc"
#undef CHANNEL

        if (budgetExhausted) {
            Logger::get().error()
                << "Item budget of " << policy.itemBudgetS
                << "s exhausted, giving up on " << task.filePath << endl;
            queueDrop(task);
            continue;
        }

        // Every enabled channel either uploaded or failed permanently
        if ((doneMask & enabledMask) == enabledMask) {
            if ((successMask & enabledMask) != 0) {
                Logger::get().info()
                    << "Upload complete: " << task.filePath << endl;
            } else {
                Logger::get().error()
                    << "All uploads failed for " << task.filePath << endl;
            }
            queueDrop(task);
            continue;
        }

        if (attempt >= maxAttempts) {
            Logger::get().error()
                << "Upload failed after " << attempt << " attempt(s): "
                << task.filePath << endl;
            queueDrop(task);
            continue;
        }

        // Hand the file back to the queue and try again in a later loop
        const u64 delayMs = retryDelayMs(policy, attempt, retryAfterSec);
        queueReschedule(task, doneMask, successMask, firstAttemptTick,
                        armGetSystemTick() + msToTicks(delayMs));
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

    // Get the initial last file for the standard album (for comparison)
    // If album is not ready (Err), we'll use the first valid item later
    auto lastAlbumResult = getLastAlbumItem();
    if (lastAlbumResult.has_value()) {
        Logger::get().info()
            << "Current last album item: " << lastAlbumResult.value() << endl;
    } else {
        Logger::get().info()
            << "Album not ready: " << lastAlbumResult.error() << endl;
    }

    // PNGShot compatibility: also track PNGShot directory
    const bool pngshotEnabled = Config::get().pngshotEnabled();
    auto lastPNGShotResult =
        std::expected<std::string, std::string>(std::unexpected(""));
    if (pngshotEnabled) {
        Logger::get().info() << "PNGShot compatibility mode enabled" << endl;
        lastPNGShotResult = getLastAlbumItem("img:/PNGs/");
        if (lastPNGShotResult.has_value()) {
            Logger::get().info()
                << "Current last PNGShot item: " << lastPNGShotResult.value()
                << endl;
        } else {
            Logger::get().info()
                << "PNGShot album not ready: " << lastPNGShotResult.error()
                << endl;
        }
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

        std::vector<std::string> allItems;

        // 1. Collect from standard album (img:/)
        const std::string lastAlbumPath =
            lastAlbumResult.has_value() ? lastAlbumResult.value() : "";
        auto albumItems = getNewAlbumItems(lastAlbumPath);
        if (albumItems.has_value()) {
            for (const auto& item : albumItems.value()) {
                // When pngshot is enabled, only collect videos from img:/
                if (!pngshotEnabled || isVideoFile(item)) {
                    allItems.push_back(item);
                }
            }
        }

        // 2. Collect from PNGShot directory (img:/PNGs/)
        if (pngshotEnabled) {
            const std::string lastPNGShotPath =
                lastPNGShotResult.has_value() ? lastPNGShotResult.value() : "";
            auto pngItems = getNewAlbumItems(lastPNGShotPath, "img:/PNGs/");
            if (pngItems.has_value()) {
                for (const auto& item : pngItems.value()) {
                    allItems.push_back(item);
                }
            }
        }

        // 3. Sort merged results chronologically
        if (!allItems.empty()) {
            std::sort(allItems.begin(), allItems.end());

            // Process all new items
            for (const auto& item : allItems) {
                if (filesize(item) > 0) {
                    if (queueAdd(item.c_str())) {
                        Logger::get().info()
                            << "New: " << item << " (queue: " << queueCount()
                            << ")" << endl;

                        // Update the appropriate tracker based on path prefix
                        if (pngshotEnabled && item.starts_with("img:/PNGs/")) {
                            lastPNGShotResult = item;
                        } else {
                            lastAlbumResult = item;
                        }
                    } else {
                        Logger::get().error()
                            << "Queue full, skipping: " << item << endl;
                        // Do not update tracker - we'll retry this item on
                        // next iteration
                    }
                }
            }
        }

        // Attempt the uploads that are due, including retry rounds handed over
        // from an earlier loop iteration
        if (queueCount() > 0) {
            processDueUploads();
        }

        // The detection cadence stays fixed: retries are picked up by a later
        // iteration instead of shortening the loop period
        svcSleepThread(sleepDuration);
    }
}
