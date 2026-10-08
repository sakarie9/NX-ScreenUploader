#pragma once

#include <cstddef>
#include <cstdint>

// Upload tasks live in a fixed-size pool, no heap allocation. The pool has to
// hold both newly detected files and files waiting for a retry round, so it is
// larger than the number of files detected in one loop iteration.
constexpr size_t MAX_QUEUE_SIZE = 16;
constexpr size_t MAX_TASK_PATH = 128;

// A file waiting for its next upload attempt
struct UploadTask {
    char filePath[MAX_TASK_PATH];
    bool valid;
    uint64_t sequence;          // insertion order, oldest first
    uint8_t round;              // completed attempt rounds
    uint8_t doneMask;           // channels that need no further attempts
    uint8_t successMask;        // channels that uploaded successfully
    uint64_t firstAttemptTick;  // 0 until the first attempt ran
    uint64_t nextAttemptTick;   // earliest tick for the next round
};

// Snapshot of a task that is due for an upload attempt
struct DueUpload {
    size_t index;
    char filePath[MAX_TASK_PATH];
    uint8_t round;
    uint8_t doneMask;
    uint8_t successMask;
    uint64_t firstAttemptTick;
};

// Initialize the upload queue and mutex
void queueInit();

// Add a task to the queue
// Returns true if successfully added, false if queue is full
[[nodiscard]] bool queueAdd(const char* filePath);

// Copy out the oldest task that is due at nowTick.
// Returns false when no task is due. A taken task stays claimed until it is
// either rescheduled or dropped.
[[nodiscard]] bool queueTakeDue(uint64_t nowTick, DueUpload& out);

// Schedule the next attempt round for a task
void queueReschedule(const DueUpload& task, uint8_t doneMask,
                     uint8_t successMask, uint64_t firstAttemptTick,
                     uint64_t nextAttemptTick);

// Remove a finished task from the queue
void queueDrop(const DueUpload& task);

// Number of tasks in the queue, including the ones waiting for a retry
[[nodiscard]] size_t queueCount();
