#include "core/models.h"

#include <array>
#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>
#include <utility>

namespace command_runner {
namespace {

std::wstring makeId() {
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

double nowSeconds() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration<double>(now).count();
}

}  // namespace

CommandConfig::CommandConfig(std::wstring nameValue,
                             std::wstring workingDirectoryValue,
                             std::wstring commandLineValue,
                             std::string encodingValue,
                             std::wstring idValue,
                             bool autoStartValue,
                             bool shellValue)
    : mName(std::move(nameValue)),
      mWorkingDirectory(std::move(workingDirectoryValue)),
      mCommandLine(std::move(commandLineValue)),
      mEncoding(std::move(encodingValue)),
      mId(std::move(idValue)),
      mAutoStart(autoStartValue),
      mShell(shellValue) {
    if (mId.empty()) {
        mId = makeId();
    }
}

LogLine LogLine::create(std::uint64_t sequence, std::string stream, std::wstring text) {
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n')) {
        text.pop_back();
    }
    return LogLine{sequence, nowSeconds(), std::move(stream), std::move(text)};
}

std::wstring stateToString(State state) {
    switch (state) {
    case State::STOPPED:
        return L"STOPPED";
    case State::STARTING:
        return L"STARTING";
    case State::RUNNING:
        return L"RUNNING";
    case State::STOPPING:
        return L"STOPPING";
    case State::EXITED:
        return L"EXITED";
    case State::FAILED:
        return L"FAILED";
    }
    return L"FAILED";
}

}  // namespace command_runner
