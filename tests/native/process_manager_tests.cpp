#include "core/process_manager.h"

#include <windows.h>

#include <cassert>
#include <chrono>
#include <print>
#include <string>
#include <thread>

namespace {

using command_runner::CommandConfig;
using command_runner::ProcessManager;
using command_runner::State;

constexpr auto POLL_INTERVAL = std::chrono::milliseconds(20);
constexpr auto TEST_TIMEOUT = std::chrono::seconds(8);

template <typename Predicate>
void waitUntil(Predicate&& predicate) {
    const auto deadline = std::chrono::steady_clock::now() + TEST_TIMEOUT;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return;
        }
        std::this_thread::sleep_for(POLL_INTERVAL);
    }
    assert(false && "timed out waiting for ProcessManager");
}

void testCommandLineNormalization() {
    assert(ProcessManager::normalizeCommandLine(
               L"echo one\r\necho two\necho three\recho four") ==
           L"echo one echo two echo three echo four");
}

void testCaptureAndTailLine() {
    ProcessManager manager;
    const CommandConfig command(
        L"capture",
        L".",
        L"cmd.exe /D /C \"echo direct-out&echo direct-err 1>&2&echo|set /p=tail&exit /b 0\"",
        "utf-8",
        L"native-process-capture");
    manager.start(command);

    waitUntil([&] {
        const State state = manager.snapshot(command.mId).mState;
        return state == State::EXITED || state == State::FAILED;
    });
    const auto snapshot = manager.snapshot(command.mId);
    assert(snapshot.mState == State::EXITED);
    assert(snapshot.mExitCode == 0);
    assert(!snapshot.mStdoutLines.empty());
    assert(!snapshot.mStderrLines.empty());
    assert(snapshot.mStdoutLines.back().mText == L"tail");
    assert(snapshot.mStderrLines.front().mText == L"direct-err ");
    assert(snapshot.mCombinedLines.size() == 3);
}

void testShellMode() {
    ProcessManager manager;
    const CommandConfig command(
        L"shell",
        L".",
        L"echo shell-out&echo shell-err 1>&2",
        "system",
        L"native-process-shell",
        false,
        true);
    manager.start(command);

    waitUntil([&] {
        const State state = manager.snapshot(command.mId).mState;
        return state == State::EXITED || state == State::FAILED;
    });
    const auto snapshot = manager.snapshot(command.mId);
    assert(snapshot.mState == State::EXITED);
    assert(snapshot.mStdoutLines.front().mText == L"shell-out");
    assert(snapshot.mStderrLines.front().mText == L"shell-err ");
}

void testStopAndRestart() {
    ProcessManager manager;
    const CommandConfig command(
        L"long-running",
        L".",
        L"cmd.exe /D /C \"ping.exe -n 30 127.0.0.1 > nul\"",
        "auto",
        L"native-process-stop");
    manager.start(command);
    waitUntil([&] {
        return manager.snapshot(command.mId).mState == State::RUNNING;
    });

    manager.stop(command.mId, std::chrono::milliseconds(100));
    waitUntil([&] {
        return manager.snapshot(command.mId).mState == State::STOPPED;
    });
    assert(manager.snapshot(command.mId).mPid == std::nullopt);

    manager.restart(command);
    waitUntil([&] {
        return manager.snapshot(command.mId).mState == State::RUNNING;
    });
    manager.stop(command.mId, std::chrono::milliseconds(100));
    waitUntil([&] {
        return manager.snapshot(command.mId).mState == State::STOPPED;
    });
}

}  // namespace

int main() {
    testCommandLineNormalization();
    testCaptureAndTailLine();
    testShellMode();
    testStopAndRestart();
    std::print("ProcessManager tests passed\n");
    return 0;
}
