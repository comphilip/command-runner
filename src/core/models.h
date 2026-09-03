#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace command_runner {

enum class State {
    STOPPED,
    STARTING,
    RUNNING,
    STOPPING,
    EXITED,
    FAILED,
};

struct Preferences {
    bool mWrapLines{false};
    bool mAutoScroll{true};

    friend bool operator==(const Preferences&, const Preferences&) = default;
};

struct CommandConfig {
    std::wstring mName;
    std::wstring mWorkingDirectory;
    std::wstring mCommandLine;
    std::string mEncoding{"auto"};
    std::wstring mId;
    bool mAutoStart{false};
    bool mShell{false};

    CommandConfig() = default;
    CommandConfig(std::wstring nameValue,
                  std::wstring workingDirectoryValue,
                  std::wstring commandLineValue,
                  std::string encodingValue = "auto",
                  std::wstring idValue = {},
                  bool autoStartValue = false,
                  bool shellValue = false);

    friend bool operator==(const CommandConfig&, const CommandConfig&) = default;
};

struct LogLine {
    std::uint64_t mSequence{0};
    double mTimestamp{0.0};
    std::string mStream;
    std::wstring mText;

    static LogLine create(std::uint64_t sequence, std::string stream, std::wstring text);

    friend bool operator==(const LogLine&, const LogLine&) = default;
};

struct RuntimeSnapshot {
    State mState{State::STOPPED};
    std::optional<std::uint32_t> mPid;
    std::optional<std::int32_t> mExitCode;
    std::vector<LogLine> mStdoutLines;
    std::vector<LogLine> mStderrLines;
    std::vector<LogLine> mCombinedLines;
    std::uint64_t mClearedThrough{0};

    friend bool operator==(const RuntimeSnapshot&, const RuntimeSnapshot&) = default;
};

struct StateChanged {
    std::wstring mCommandId;
    std::uint64_t mGeneration{0};
};

struct LogAdded {
    std::wstring mCommandId;
    LogLine mLine;
    std::uint64_t mGeneration{0};
};

using ProcessEvent = std::variant<StateChanged, LogAdded>;

std::wstring stateToString(State state);

}  // namespace command_runner
