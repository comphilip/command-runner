#pragma once

#include "core/models.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace command_runner {

class ProcessManager final {
public:
    ProcessManager();
    ~ProcessManager();

    ProcessManager(const ProcessManager&) = delete;
    ProcessManager& operator=(const ProcessManager&) = delete;
    ProcessManager(ProcessManager&&) = delete;
    ProcessManager& operator=(ProcessManager&&) = delete;

    // Start, stop, and restart are asynchronous. StateChanged and LogAdded
    // events are available through drainEvents().
    void start(const CommandConfig& config);
    void stop(std::wstring_view commandId,
              std::chrono::milliseconds timeout = std::chrono::seconds(4));
    void restart(const CommandConfig& config);

    [[nodiscard]] RuntimeSnapshot snapshot(std::wstring_view commandId) const;
    [[nodiscard]] std::vector<ProcessEvent> drainEvents();
    [[nodiscard]] std::vector<std::wstring> runningIds() const;

    void clearLogs(std::wstring_view commandId);
    void close();

    [[nodiscard]] static std::wstring normalizeCommandLine(
        std::wstring_view commandLine);

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

}  // namespace command_runner
