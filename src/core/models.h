#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace command_runner {

enum class State {
    Stopped,
    Starting,
    Running,
    Stopping,
    Exited,
    Failed,
};

struct Preferences {
    bool wrap_lines{false};
    bool auto_scroll{true};

    friend bool operator==(const Preferences&, const Preferences&) = default;
};

struct CommandConfig {
    std::wstring name;
    std::wstring working_directory;
    std::wstring command_line;
    std::string encoding{"auto"};
    std::wstring id;
    bool auto_start{false};
    bool shell{false};

    CommandConfig() = default;
    CommandConfig(std::wstring name_value,
                  std::wstring working_directory_value,
                  std::wstring command_line_value,
                  std::string encoding_value = "auto",
                  std::wstring id_value = {},
                  bool auto_start_value = false,
                  bool shell_value = false);

    friend bool operator==(const CommandConfig&, const CommandConfig&) = default;
};

struct LogLine {
    std::uint64_t sequence{0};
    double timestamp{0.0};
    std::string stream;
    std::wstring text;

    static LogLine create(std::uint64_t sequence, std::string stream, std::wstring text);

    friend bool operator==(const LogLine&, const LogLine&) = default;
};

struct RuntimeSnapshot {
    State state{State::Stopped};
    std::optional<std::uint32_t> pid;
    std::optional<std::int32_t> exit_code;
    std::vector<LogLine> stdout_lines;
    std::vector<LogLine> stderr_lines;
    std::vector<LogLine> combined_lines;
    std::uint64_t cleared_through{0};

    friend bool operator==(const RuntimeSnapshot&, const RuntimeSnapshot&) = default;
};

struct StateChanged {
    std::wstring command_id;
};

struct LogAdded {
    std::wstring command_id;
    LogLine line;
};

using ProcessEvent = std::variant<StateChanged, LogAdded>;

std::wstring state_to_string(State state);

}  // namespace command_runner
