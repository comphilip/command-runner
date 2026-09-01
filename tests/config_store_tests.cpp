#include "core/config_store.h"

#include <windows.h>

#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <print>
#include <stdexcept>
#include <string>

namespace {

using command_runner::CommandConfig;
using command_runner::ConfigData;
using command_runner::ConfigStore;
using command_runner::Preferences;

std::filesystem::path testRoot() {
    std::array<wchar_t, MAX_PATH> path{};
    const DWORD length = GetTempPathW(static_cast<DWORD>(path.size()), path.data());
    if (length == 0 || length >= path.size()) {
        throw std::runtime_error("GetTempPathW failed");
    }
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::path(std::wstring(path.data(), length)) /
           (L"CommandRunner.ConfigStore." + std::to_wstring(suffix));
}

void writeText(const std::filesystem::path& path, const std::string& value) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    assert(output);
    output << value;
    assert(output.good());
}

void testDefaultsAndLegacyFields(const std::filesystem::path& root) {
    const auto path = root / L"legacy.json";
    writeText(path, R"json({
      "version": 1,
      "commands": [{
        "id": "legacy-id",
        "name": "Unicode 名称",
        "working_directory": "C:/work",
        "command_line": "echo hello",
        "encoding": "utf-8",
        "execution_mode": "shell",
        "future_field": true
      }],
      "preferences": {"future_preference": false}
    })json");

    const auto result = ConfigStore(path).load();
    assert(result.has_value());
    assert(result->mCommands.size() == 1);
    assert(result->mCommands[0].mId == L"legacy-id");
    assert(result->mCommands[0].mName == L"Unicode 名称");
    assert(!result->mCommands[0].mAutoStart);
    assert(!result->mCommands[0].mShell);
    assert(result->mPreferences == Preferences(false, true));
}

void testRoundTripAndAtomicSave(const std::filesystem::path& root) {
    const auto path = root / L"round-trip.json";
    const CommandConfig command(L"演示", root.wstring(), L"echo \"hello world\"",
                                "utf-8", L"stable-id", true, true);
    const ConfigData input{{command}, Preferences(true, false)};

    const auto saveResult = ConfigStore(path).save(input);
    assert(saveResult.has_value());
    assert(!std::filesystem::exists(std::filesystem::path(path.wstring() + L".tmp")));

    const auto output = ConfigStore(path).load();
    assert(output.has_value());
    assert(output->mCommands.size() == 1);
    assert(output->mCommands[0] == command);
    assert(output->mPreferences == input.mPreferences);
}

void testCorruptFileIsSafeAndUntouched(const std::filesystem::path& root) {
    const auto path = root / L"corrupt.json";
    const std::string original = "{ not valid json";
    writeText(path, original);

    const auto result = ConfigStore(path).load();
    assert(!result.has_value());
    assert(!result.error().empty());

    std::ifstream input(path, std::ios::binary);
    const std::string after((std::istreambuf_iterator<char>(input)),
                            std::istreambuf_iterator<char>());
    assert(after == original);
}

void testSaveFailureDoesNotReplaceDestination(const std::filesystem::path& root) {
    const auto parentThatIsAFile = root / L"not-a-directory";
    writeText(parentThatIsAFile, "keep me");
    const auto path = parentThatIsAFile / L"commands.json";

    const auto saveResult = ConfigStore(path).save(ConfigData{});
    assert(!saveResult.has_value());
    assert(!saveResult.error().empty());
    std::ifstream input(parentThatIsAFile, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    assert(contents == "keep me");
}

}  // namespace

int main() {
    const auto root = testRoot();
    std::filesystem::create_directories(root);
    try {
        testDefaultsAndLegacyFields(root);
        testRoundTripAndAtomicSave(root);
        testCorruptFileIsSafeAndUntouched(root);
        testSaveFailureDoesNotReplaceDestination(root);
        std::filesystem::remove_all(root);
        std::print("ConfigStore tests passed\n");
        return 0;
    } catch (...) {
        std::filesystem::remove_all(root);
        throw;
    }
}
