#include <curl/curl.h>
#include <dirent.h>
#include <netinet/in.h>
#include <switch.h>

#include <array>
#include <iostream>
#include <string>
#include <string_view>

#include "config.hpp"
#include "logger.hpp"
#include "project.h"
#include "upload.hpp"
#include "utils.hpp"

// Reduce heap size for memory optimization
// 0x40000 (256KB) will oom, 0x50000 (320KB) is minimum stable
#define INNER_HEAP_SIZE 0x50000

extern "C" {

extern u32 __start__;

// Sysmodules should not use applet*.
u32 __nx_applet_type = AppletType_None;

// Minimize fs resource usage for memory optimization
u32 __nx_fs_num_sessions = 1;
u32 __nx_fsdev_direntry_cache_size = 1;
bool __nx_fsdev_support_cwd = false;

// Minimize system resource allocation
u32 __nx_nv_transfermem_size = 0;  // We don't use NV services

void __libnx_init_time(void);
void __libnx_initheap(void);
void __appInit(void);
void __appExit(void);

// Newlib heap configuration function (makes malloc/free work).
void __libnx_initheap(void) {
    static char g_innerheap[INNER_HEAP_SIZE];

    extern char* fake_heap_start;
    extern char* fake_heap_end;

    fake_heap_start = &g_innerheap[0];
    fake_heap_end   = &g_innerheap[sizeof g_innerheap];
}

#define TCP_TX_BUF_SIZE (1024 * 2)
#define TCP_RX_BUF_SIZE (1024 * 2)
#define TCP_TX_BUF_SIZE_MAX (1024 * 16)
#define TCP_RX_BUF_SIZE_MAX (1024 * 16)
#define UDP_TX_BUF_SIZE (0)
#define UDP_RX_BUF_SIZE (0)
#define SB_EFFICIENCY (1)

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
    rc = pmdmntInitialize();
    if (R_FAILED(rc)) {
        fatalThrow(rc);
    }
    rc = nsInitialize();
    if (R_FAILED(rc)) {
        fatalThrow(rc);
    }
    SocketInitConfig sockConf = {
        .tcp_tx_buf_size = TCP_TX_BUF_SIZE,
        .tcp_rx_buf_size = TCP_RX_BUF_SIZE,
        .tcp_tx_buf_max_size = TCP_TX_BUF_SIZE_MAX,
        .tcp_rx_buf_max_size = TCP_RX_BUF_SIZE_MAX,

        .udp_tx_buf_size = UDP_TX_BUF_SIZE,
        .udp_rx_buf_size = UDP_RX_BUF_SIZE,

        .sb_efficiency = SB_EFFICIENCY,
    };
    rc = socketInitialize(&sockConf);
    if (R_FAILED(rc)) {
        fatalThrow(rc);
    }

    rc = pminfoInitialize();
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

    rc = timeInitialize();
    if (R_FAILED(rc)) {
        fatalThrow(rc);
    }

    fsdevMountSdmc();
}

void __appExit(void) {
    fsdevUnmountAll();
    timeExit();
    fsExit();
    capsaExit();
    pminfoExit();
    pmdmntExit();
    nsExit();
    socketExit();
    smExit();
}
}

void initLogger(bool truncate) {
    if (truncate) {
        Logger::get().truncate();
    }

    constexpr std::string_view separator = "=============================";
    auto& logger = Logger::get().none();
    logger << std::endl
           << separator << std::endl
           << APP_TITLE " v" << APP_VERSION << " is starting..." << std::endl
           << separator << std::endl;
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

    curl_global_init(CURL_GLOBAL_ALL);

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

    // 获取初始的最后一个文件（用于对比）
    std::string lastItem = getLastAlbumItem();
    Logger::get().info() << "Current last item: " << lastItem << std::endl;

    // 根据配置确定上传模式
    const UploadMode uploadMode = Config::get().getUploadMode();
    const char* modeStr = uploadMode == UploadMode::Compressed ? "compressed"
                          : uploadMode == UploadMode::Original ? "original"
                                                               : "both";
    Logger::get().info() << "Upload mode: " << modeStr << std::endl;

    // 获取检查间隔配置
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

        // auto& logger = Logger::get().info();
        // logger << "ping" << std::endl;

        if (lastItem < tmpItem) {
            const size_t fs = filesize(tmpItem);

            if (fs > 0) {
                auto& logger = Logger::get().info();
                logger << separator << std::endl
                       << "New item found: " << tmpItem << std::endl
                       << "Filesize: " << fs << std::endl;

                bool sent = false;

                // 根据配置模式决定上传策略
                switch (uploadMode) {
                    case UploadMode::Compressed:
                        // 只尝试压缩上传
                        for (int retry = 0; retry < maxRetries && !sent;
                             ++retry) {
                            sent = sendFileToServer(tmpItem, fs, true);
                        }
                        break;

                    case UploadMode::Original:
                        // 只尝试原图上传
                        for (int retry = 0; retry < maxRetries && !sent;
                             ++retry) {
                            sent = sendFileToServer(tmpItem, fs, false);
                        }
                        break;

                    case UploadMode::Both:
                        // 先尝试压缩，失败则尝试原图
                        for (int retry = 0; retry < maxRetries && !sent;
                             ++retry) {
                            sent = sendFileToServer(tmpItem, fs, true);
                        }
                        if (!sent) {
                            for (int retry = 0; retry < maxRetries && !sent;
                                 ++retry) {
                                sent = sendFileToServer(tmpItem, fs, false);
                            }
                        }
                        break;
                }

                if (!sent) {
                    Logger::get().error()
                        << "Unable to send file after " << maxRetries
                        << " retries, skipping..." << std::endl;
                }

                // 无论成功与否都更新lastItem，避免无限重试同一文件
                lastItem = std::move(tmpItem);
            }

            Logger::get().close();
        }

        svcSleepThread(sleepDuration);
    }
}
