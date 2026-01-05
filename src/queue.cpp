#include "queue.hpp"

#include <switch.h>

#include <cstring>

namespace {
UploadTask g_uploadQueue[MAX_QUEUE_SIZE];
size_t g_queueHead = 0;  // Read position
size_t g_queueTail = 0;  // Write position
size_t g_queueCount = 0;

Mutex g_queueMutex;
}  // namespace

void queueInit() { mutexInit(&g_queueMutex); }

bool queueAdd(const char* filePath, size_t fileSize) {
    mutexLock(&g_queueMutex);

    if (g_queueCount >= MAX_QUEUE_SIZE) {
        mutexUnlock(&g_queueMutex);
        return false;
    }

    std::strncpy(g_uploadQueue[g_queueTail].filePath, filePath, 127);
    g_uploadQueue[g_queueTail].filePath[127] = '\0';
    g_uploadQueue[g_queueTail].fileSize = fileSize;
    g_queueTail = (g_queueTail + 1) % MAX_QUEUE_SIZE;
    g_queueCount++;

    mutexUnlock(&g_queueMutex);
    return true;
}

bool queueGet(char* filePath, size_t filePath_size, size_t& fileSize) {
    mutexLock(&g_queueMutex);

    if (g_queueCount == 0) {
        mutexUnlock(&g_queueMutex);
        return false;
    }

    std::strncpy(filePath, g_uploadQueue[g_queueHead].filePath,
                 filePath_size - 1);
    filePath[filePath_size - 1] = '\0';
    fileSize = g_uploadQueue[g_queueHead].fileSize;
    g_queueHead = (g_queueHead + 1) % MAX_QUEUE_SIZE;
    g_queueCount--;

    mutexUnlock(&g_queueMutex);
    return true;
}

size_t queueCount() {
    mutexLock(&g_queueMutex);
    size_t count = g_queueCount;
    mutexUnlock(&g_queueMutex);
    return count;
}
