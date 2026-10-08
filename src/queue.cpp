#include "queue.hpp"

#include <switch.h>

#include <cstring>

namespace {
UploadTask g_tasks[MAX_QUEUE_SIZE];
uint64_t g_sequence = 0;
Mutex g_queueMutex;
}  // namespace

void queueInit() { mutexInit(&g_queueMutex); }

bool queueAdd(const char* filePath) {
    mutexLock(&g_queueMutex);

    // Find a free slot
    UploadTask* slot = nullptr;
    for (auto& task : g_tasks) {
        if (!task.valid) {
            slot = &task;
            break;
        }
    }

    if (slot == nullptr) {
        mutexUnlock(&g_queueMutex);
        return false;
    }

    std::strncpy(slot->filePath, filePath, MAX_TASK_PATH - 1);
    slot->filePath[MAX_TASK_PATH - 1] = '\0';
    slot->valid = true;
    slot->sequence = g_sequence++;
    slot->round = 0;
    slot->doneMask = 0;
    slot->successMask = 0;
    slot->firstAttemptTick = 0;
    slot->nextAttemptTick = 0;  // due immediately

    mutexUnlock(&g_queueMutex);
    return true;
}

bool queueTakeDue(uint64_t nowTick, DueUpload& out) {
    mutexLock(&g_queueMutex);

    UploadTask* found = nullptr;
    size_t foundIndex = 0;

    for (size_t i = 0; i < MAX_QUEUE_SIZE; ++i) {
        UploadTask& task = g_tasks[i];
        if (!task.valid || task.nextAttemptTick > nowTick) {
            continue;
        }
        // Oldest file first
        if (found == nullptr || task.sequence < found->sequence) {
            found = &task;
            foundIndex = i;
        }
    }

    if (found == nullptr) {
        mutexUnlock(&g_queueMutex);
        return false;
    }

    out.index = foundIndex;
    std::strncpy(out.filePath, found->filePath, MAX_TASK_PATH - 1);
    out.filePath[MAX_TASK_PATH - 1] = '\0';
    out.round = found->round;
    out.doneMask = found->doneMask;
    out.successMask = found->successMask;
    out.firstAttemptTick = found->firstAttemptTick;

    // Claim the task so that it cannot be picked up twice, even if the caller
    // fails to reschedule or drop it
    found->nextAttemptTick = UINT64_MAX;

    mutexUnlock(&g_queueMutex);
    return true;
}

void queueReschedule(const DueUpload& task, uint8_t doneMask,
                     uint8_t successMask, uint64_t firstAttemptTick,
                     uint64_t nextAttemptTick) {
    mutexLock(&g_queueMutex);

    UploadTask& slot = g_tasks[task.index];
    if (slot.valid) {
        slot.round = static_cast<uint8_t>(task.round + 1);
        slot.doneMask = doneMask;
        slot.successMask = successMask;
        slot.firstAttemptTick = firstAttemptTick;
        slot.nextAttemptTick = nextAttemptTick;
    }

    mutexUnlock(&g_queueMutex);
}

void queueDrop(const DueUpload& task) {
    mutexLock(&g_queueMutex);
    g_tasks[task.index].valid = false;
    mutexUnlock(&g_queueMutex);
}

size_t queueCount() {
    mutexLock(&g_queueMutex);

    size_t count = 0;
    for (const auto& task : g_tasks) {
        if (task.valid) {
            ++count;
        }
    }

    mutexUnlock(&g_queueMutex);
    return count;
}
