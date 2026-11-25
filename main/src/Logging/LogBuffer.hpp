#pragma once

#include <cstddef>
#include <cstdint>

namespace opencomfort {

constexpr size_t kLogEntries = 128;
constexpr size_t kLogTagLen = 23;
constexpr size_t kLogMessageLen = 111;

struct LogEntry {
    uint32_t seq = 0;
    uint32_t ms = 0;    // uptime in ms, as ESP_LOG prints it
    char level = 'I';   // E, W, I, D or V
    char tag[kLogTagLen + 1] = {};
    char message[kLogMessageLen + 1] = {};
};

// Splits one ESP_LOG line ("\033[0;32mI (1234) Tag: text\033[0m\n") into an entry.
// Returns false for output that is not a log line (plain printf, partial writes).
bool parseLogLine(const char* line, LogEntry& out);

// Keeps the last kLogEntries ESP_LOG lines in RAM for the portal. Serial output is kept.
class LogBuffer {
public:
    static void install();  // call first thing at boot
    // Copies entries with seq > since, oldest first. Returns how many were copied and sets
    // `dropped` when older lines than `since` were overwritten before they were read.
    static size_t read(uint32_t since, LogEntry* out, size_t capacity, bool& dropped);
    static uint32_t lastSeq();
    static void setDebug(bool debug);
    static bool debug();
};

}  // namespace opencomfort
