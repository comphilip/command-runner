#include "core/config_store.h"

#include <nlohmann/json.hpp>

#include <windows.h>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {

using command_runner::CommandConfig;
using command_runner::ConfigData;
using command_runner::ConfigStore;
using command_runner::Preferences;

std::filesystem::path test_root() {
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetTempPathW(MAX_PATH, path);
    if (length == 0 || length >= MAX_PATH) {
        throw std::runtime_error("GetTempPathW failed");
    }
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::path(path) /
           (L"CommandRunner.ConfigStore." + std::to_wstring(suffix));
}

void write_text(const std::filesystem::path& path, const std::string& value) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    assert(output);
    output << value;
    assert(output.good());
}

void test_defaults_and_legacy_fields(const std::filesystem::path& root) {
    const auto path = root / L"legacy.json";
    write_text(path, R"json({
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
    assert(result.succeeded());
    assert(result.data.commands.size() == 1);
    assert(result.data.commands[0].id == L"legacy-id");
    assert(result.data.commands[0].name == L"Unicode 名称");
    assert(!result.data.commands[0].auto_start);
    assert(!result.data.commands[0].shell);
    assert(result.data.preferences == Preferences(false, true));
}

void test_round_trip_and_atomic_save(const std::filesystem::path& root) {
    const auto path = root / L"round-trip.json";
    const CommandConfig command(L"演示", root.wstring(), L"echo \"hello world\"",
                                "utf-8", L"stable-id", true, true);
    const ConfigData input{{command}, Preferences(true, false)};

    std::wstring error;
    assert(ConfigStore(path).save(input, &error));
    assert(error.empty());
    assert(!std::filesystem::exists(std::filesystem::path(path.wstring() + L".tmp")));

    const auto output = ConfigStore(path).load();
    assert(output.succeeded());
    assert(output.data.commands.size() == 1);
    assert(output.data.commands[0] == command);
    assert(output.data.preferences == input.preferences);
}

void test_corrupt_file_is_safe_and_untouched(const std::filesystem::path& root) {
    const auto path = root / L"corrupt.json";
    const std::string original = "{ not valid json";
    write_text(path, original);

    const auto result = ConfigStore(path).load();
    assert(!result.succeeded());
    assert(result.data.commands.empty());
    assert(result.data.preferences == Preferences(false, true));

    std::ifstream input(path, std::ios::binary);
    const std::string after((std::istreambuf_iterator<char>(input)),
                            std::istreambuf_iterator<char>());
    assert(after == original);
}

void test_save_failure_does_not_replace_destination(const std::filesystem::path& root) {
    const auto parent_that_is_a_file = root / L"not-a-directory";
    write_text(parent_that_is_a_file, "keep me");
    const auto path = parent_that_is_a_file / L"commands.json";

    std::wstring error;
    assert(!ConfigStore(path).save(ConfigData{}, &error));
    assert(!error.empty());
    std::ifstream input(parent_that_is_a_file, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    assert(contents == "keep me");
}

}  // namespace

int main() {
    const auto root = test_root();
    std::filesystem::create_directories(root);
    try {
        test_defaults_and_legacy_fields(root);
        test_round_trip_and_atomic_save(root);
        test_corrupt_file_is_safe_and_untouched(root);
        test_save_failure_does_not_replace_destination(root);
        std::filesystem::remove_all(root);
        std::cout << "ConfigStore tests passed\n";
        return 0;
    } catch (...) {
        std::filesystem::remove_all(root);
        throw;
    }
}
