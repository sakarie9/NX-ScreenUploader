#pragma once

#include <cstddef>
#include <cstdint>

// Upload task with fixed-size buffer (no heap allocation)
struct UploadTask {
    char filePath[128];  // Fixed buffer for path
    bool valid;
};

constexpr size_t MAX_QUEUE_SIZE = 8;

// Initialize the upload queue and mutex
void queueInit();

// Add a task to the queue
// Returns true if successfully added, false if queue is full
[[nodiscard]] bool queueAdd(const char* filePath);

// Try to get the next task from the queue
// Returns true if a task was retrieved, false if queue is empty
[[nodiscard]] bool queueGet(char* filePath, size_t filePath_size);

// Get the current number of items in the queue
[[nodiscard]] size_t queueCount();
