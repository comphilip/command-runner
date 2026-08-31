#include "core/models.h"

#include <array>
#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>
#include <utility>

namespace command_runner {
namespace {

std::wstring make_id() {
    std::array<std::uint8_t, 16> bytes{};
    std::random_device random;
    for (auto& byte : bytes) {
        byte = static_cast<std::uint8_t>(random());
    }

    // RFC 9562 UUID version 4 and variant bits.
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0F) | 0x40);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3F) | 0x80);

    std::wostringstream result;
    result << std::hex << std::setfill(L'0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        result << std::setw(2) << static_cast<unsigned>(bytes[index]);
        if (index == 3 || index == 5 || index == 7 || index == 9) {
            result << L'-';
        }
    }
    return result.str();
}

double now_seconds() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration<double>(now).count();
}

}  // namespace

CommandConfig::CommandConfig(std::wstring name_value,
                             std::wstring working_directory_value,
                             std::wstring command_line_value,
                             std::string encoding_value,
                             std::wstring id_value,
                             bool auto_start_value,
                             bool shell_value)
    : name(std::move(name_value)),
      working_directory(std::move(working_directory_value)),
      command_line(std::move(command_line_value)),
      encoding(std::move(encoding_value)),
      id(std::move(id_value)),
      auto_start(auto_start_value),
      shell(shell_value) {
    if (id.empty()) {
        id = make_id();
    }
}

LogLine LogLine::create(std::uint64_t sequence, std::string stream, std::wstring text) {
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n')) {
        text.pop_back();
    }
    return LogLine{sequence, now_seconds(), std::move(stream), std::move(text)};
}

std::wstring state_to_string(State state) {
    switch (state) {
    case State::Stopped:
        return L"STOPPED";
    case State::Starting:
        return L"STARTING";
    case State::Running:
        return L"RUNNING";
    case State::Stopping:
        return L"STOPPING";
    case State::Exited:
        return L"EXITED";
    case State::Failed:
        return L"FAILED";
    }
    return L"FAILED";
}

}  // namespace command_runner
