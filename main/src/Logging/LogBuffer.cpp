#include "src/Logging/LogBuffer.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
}

namespace opencomfort {
namespace {

LogEntry s_entries[kLogEntries];
uint32_t s_nextSeq = 1;
portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
vprintf_like_t s_serial = nullptr;
bool s_debug = false;

int capture(const char* format, va_list args)
{
    va_list copy;
    va_copy(copy, args);
    const int written = s_serial != nullptr ? s_serial(format, args) : 0;  // keep the UART log

    char line[192];
    std::vsnprintf(line, sizeof(line), format, copy);
    va_end(copy);
    LogEntry entry;
    if (parseLogLine(line, entry)) {
        portENTER_CRITICAL(&s_lock);
        entry.seq = s_nextSeq++;
        s_entries[entry.seq % kLogEntries] = entry;
        portEXIT_CRITICAL(&s_lock);
    }
    return written;
}

}  // namespace

bool parseLogLine(const char* line, LogEntry& out)
{
    if (line == nullptr) return false;
    // Skip a colour prefix such as "\033[0;32m".
    if (line[0] == '\033') {
        const char* end = std::strchr(line, 'm');
        if (end == nullptr) return false;
        line = end + 1;
    }
    const char level = line[0];
    if (std::strchr("EWIDV", level) == nullptr || level == '\0' || line[1] != ' ' || line[2] != '(') {
        return false;
    }
    char* after = nullptr;
    const unsigned long ms = std::strtoul(line + 3, &after, 10);
    if (after == line + 3 || after[0] != ')' || after[1] != ' ') return false;
    const char* tag = after + 2;
    const char* colon = std::strstr(tag, ": ");
    if (colon == nullptr) return false;

    out = LogEntry{};
    out.level = level;
    out.ms = static_cast<uint32_t>(ms);
    const size_t tagLength = static_cast<size_t>(colon - tag) < kLogTagLen ? static_cast<size_t>(colon - tag) : kLogTagLen;
    std::memcpy(out.tag, tag, tagLength);
    const char* message = colon + 2;
    size_t length = 0;
    // Stop at the colour reset or the line end.
    while (message[length] != '\0' && message[length] != '\033' && message[length] != '\n' &&
           message[length] != '\r' && length < kLogMessageLen) {
        ++length;
    }
    std::memcpy(out.message, message, length);
    return true;
}

void LogBuffer::install()
{
    if (s_serial == nullptr) s_serial = esp_log_set_vprintf(capture);
}

size_t LogBuffer::read(uint32_t since, LogEntry* out, size_t capacity, bool& dropped)
{
    portENTER_CRITICAL(&s_lock);
    const uint32_t last = s_nextSeq - 1;
    const uint32_t oldest = last >= kLogEntries ? last - kLogEntries + 1 : 1;
    uint32_t from = since + 1;
    dropped = since != 0 && from < oldest;
    if (from < oldest) from = oldest;
    size_t count = 0;
    for (uint32_t seq = from; seq <= last && count < capacity; ++seq) {
        out[count++] = s_entries[seq % kLogEntries];
    }
    portEXIT_CRITICAL(&s_lock);
    return count;
}

uint32_t LogBuffer::lastSeq()
{
    portENTER_CRITICAL(&s_lock);
    const uint32_t last = s_nextSeq - 1;
    portEXIT_CRITICAL(&s_lock);
    return last;
}

void LogBuffer::setDebug(bool debug)
{
    s_debug = debug;
    esp_log_level_set("*", debug ? ESP_LOG_DEBUG : ESP_LOG_INFO);
    // The Wi-Fi driver is very chatty at debug level; keep it at info.
    esp_log_level_set("wifi", ESP_LOG_INFO);
}

bool LogBuffer::debug()
{
    return s_debug;
}

}  // namespace opencomfort
