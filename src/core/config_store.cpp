#include "core/config_store.h"

#include <windows.h>
#include <shlobj.h>

#include <wil/resource.h>

#include <nlohmann/json.hpp>

#include <array>
#include <exception>
#include <fstream>
#include <limits>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace command_runner {
namespace {

using json = nlohmann::json;

constexpr int CONFIGURATION_VERSION = 1;
constexpr std::size_t ERROR_MESSAGE_BUFFER_SIZE = 1024;
constexpr std::size_t LOCAL_APP_DATA_BUFFER_SIZE = 32768;

std::wstring fieldName(const char* name) {
    const std::size_t length = std::char_traits<char>::length(name);
    std::wstring result;
    result.reserve(length);
    for (std::size_t index = 0; index < length; ++index) {
        result.push_back(static_cast<wchar_t>(name[index]));
    }
    return result;
}

std::wstring fieldTypeError(const char* name, const wchar_t* expectedType) {
    return L"field '" + fieldName(name) + L"' must be " + expectedType;
}

std::expected<int, std::wstring> checkedWinLength(std::size_t length,
                                                  const wchar_t* encoding) {
    if (length > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::unexpected(std::wstring(encoding) + L" input is too large");
    }
    return static_cast<int>(length);
}

std::expected<std::wstring, std::wstring> utf8ToWide(std::string_view value) {
    if (value.empty()) {
        return std::wstring{};
    }

    const auto length = checkedWinLength(value.size(), L"UTF-8");
    if (!length) {
        return std::unexpected(length.error());
    }

    const int size = MultiByteToWideChar(CP_UTF8,
                                         MB_ERR_INVALID_CHARS,
                                         value.data(),
                                         *length,
                                         nullptr,
                                         0);
    if (size <= 0) {
        return std::unexpected(L"invalid UTF-8");
    }

    std::wstring result(static_cast<std::size_t>(size), L'\0');
    if (MultiByteToWideChar(CP_UTF8,
                            MB_ERR_INVALID_CHARS,
                            value.data(),
                            *length,
                            result.data(),
                            size) != size) {
        return std::unexpected(L"invalid UTF-8");
    }
    return result;
}

std::expected<std::string, std::wstring> wideToUtf8(std::wstring_view value) {
    if (value.empty()) {
        return std::string{};
    }

    const auto length = checkedWinLength(value.size(), L"UTF-16");
    if (!length) {
        return std::unexpected(length.error());
    }

    const int size = WideCharToMultiByte(CP_UTF8,
                                         WC_ERR_INVALID_CHARS,
                                         value.data(),
                                         *length,
                                         nullptr,
                                         0,
                                         nullptr,
                                         nullptr);
    if (size <= 0) {
        return std::unexpected(L"invalid UTF-16");
    }

    std::string result(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8,
                            WC_ERR_INVALID_CHARS,
                            value.data(),
                            *length,
                            result.data(),
                            size,
                            nullptr,
                            nullptr) != size) {
        return std::unexpected(L"invalid UTF-16");
    }
    return result;
}

std::wstring lastErrorMessage(DWORD error) {
    std::array<wchar_t, ERROR_MESSAGE_BUFFER_SIZE> buffer{};
    const DWORD flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length = FormatMessageW(flags,
                                        nullptr,
                                        error,
                                        0,
                                        buffer.data(),
                                        static_cast<DWORD>(buffer.size()),
                                        nullptr);
    std::wstring message = length == 0 ? L"Unknown Windows error"
                                      : std::wstring(buffer.data(), length);
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n')) {
        message.pop_back();
    }
    return message;
}

std::wstring pathForError(const std::filesystem::path& path) {
    return path.empty() ? L"<empty path>" : path.wstring();
}

std::wstring exceptionMessage(const std::exception& exception) {
    const auto message = utf8ToWide(exception.what());
    return message ? *message : L"Unexpected configuration error";
}

std::wstring errorForPath(const std::filesystem::path& path, const std::wstring& detail) {
    return L"Unable to process configuration file '" + pathForError(path) + L"': " + detail;
}

std::expected<std::wstring, std::wstring> requiredString(const json& object,
                                                          const char* name) {
    const auto iterator = object.find(name);
    if (iterator == object.end()) {
        return std::unexpected(L"missing required field '" + fieldName(name) + L"'");
    }
    if (!iterator->is_string()) {
        return std::unexpected(fieldTypeError(name, L"a string"));
    }
    return utf8ToWide(iterator->get<std::string>());
}

std::expected<std::wstring, std::wstring> optionalString(const json& object,
                                                         const char* name,
                                                         std::wstring defaultValue = {}) {
    const auto iterator = object.find(name);
    if (iterator == object.end()) {
        return defaultValue;
    }
    if (!iterator->is_string()) {
        return std::unexpected(fieldTypeError(name, L"a string"));
    }
    return utf8ToWide(iterator->get<std::string>());
}

std::expected<std::string, std::wstring> optionalEncoding(const json& object) {
    const auto iterator = object.find("encoding");
    if (iterator == object.end()) {
        return std::string{"auto"};
    }
    if (!iterator->is_string()) {
        return std::unexpected(fieldTypeError("encoding", L"a string"));
    }
    return iterator->get<std::string>();
}

std::expected<bool, std::wstring> optionalBool(const json& object,
                                               const char* name,
                                               bool defaultValue) {
    const auto iterator = object.find(name);
    if (iterator == object.end()) {
        return defaultValue;
    }
    if (!iterator->is_boolean()) {
        return std::unexpected(fieldTypeError(name, L"a boolean"));
    }
    return iterator->get<bool>();
}

std::expected<CommandConfig, std::wstring> commandFromJson(const json& object) {
    if (!object.is_object()) {
        return std::unexpected(L"each item in 'commands' must be an object");
    }

    const auto name = requiredString(object, "name");
    if (!name) {
        return std::unexpected(name.error());
    }
    const auto workingDirectory = requiredString(object, "working_directory");
    if (!workingDirectory) {
        return std::unexpected(workingDirectory.error());
    }
    const auto commandLine = requiredString(object, "command_line");
    if (!commandLine) {
        return std::unexpected(commandLine.error());
    }
    const auto encoding = optionalEncoding(object);
    if (!encoding) {
        return std::unexpected(encoding.error());
    }
    const auto id = optionalString(object, "id");
    if (!id) {
        return std::unexpected(id.error());
    }
    const auto autoStart = optionalBool(object, "auto_start", false);
    if (!autoStart) {
        return std::unexpected(autoStart.error());
    }
    const auto shell = optionalBool(object, "shell", false);
    if (!shell) {
        return std::unexpected(shell.error());
    }

    return CommandConfig{*name,
                         *workingDirectory,
                         *commandLine,
                         *encoding,
                         *id,
                         *autoStart,
                         *shell};
}

std::expected<json, std::wstring> commandToJson(const CommandConfig& command) {
    const auto id = wideToUtf8(command.mId);
    if (!id) {
        return std::unexpected(id.error());
    }
    const auto name = wideToUtf8(command.mName);
    if (!name) {
        return std::unexpected(name.error());
    }
    const auto workingDirectory = wideToUtf8(command.mWorkingDirectory);
    if (!workingDirectory) {
        return std::unexpected(workingDirectory.error());
    }
    const auto commandLine = wideToUtf8(command.mCommandLine);
    if (!commandLine) {
        return std::unexpected(commandLine.error());
    }

    return json{
        {"id", *id},
        {"name", *name},
        {"working_directory", *workingDirectory},
        {"command_line", *commandLine},
        {"encoding", command.mEncoding},
        {"auto_start", command.mAutoStart},
        {"shell", command.mShell},
    };
}

std::expected<json, std::wstring> dataToJson(const ConfigData& data) {
    json commands = json::array();
    for (const auto& command : data.mCommands) {
        const auto serializedCommand = commandToJson(command);
        if (!serializedCommand) {
            return std::unexpected(serializedCommand.error());
        }
        commands.push_back(*serializedCommand);
    }

    return json{
        {"version", CONFIGURATION_VERSION},
        {"commands", std::move(commands)},
        {"preferences", {
            {"wrap_lines", data.mPreferences.mWrapLines},
            {"auto_scroll", data.mPreferences.mAutoScroll},
        }},
    };
}

std::filesystem::path localAppDataPath() {
    std::array<wchar_t, LOCAL_APP_DATA_BUFFER_SIZE> value{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA",
                                                  value.data(),
                                                  static_cast<DWORD>(value.size()));
    if (length > 0 && length < value.size()) {
        return std::filesystem::path(std::wstring(value.data(), length));
    }

    wil::unique_cotaskmem_string knownFolder;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,
                                       KF_FLAG_DEFAULT,
                                       nullptr,
                                       knownFolder.put()))) {
        return std::filesystem::path(knownFolder.get());
    }

    std::error_code error;
    const auto fallback = std::filesystem::current_path(error);
    return error ? std::filesystem::path(L".") : fallback;
}

std::expected<void, std::wstring> replaceAtomically(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination) {
    const DWORD attributes = GetFileAttributesW(destination.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        if (ReplaceFileW(destination.c_str(),
                         temporary.c_str(),
                         nullptr,
                         REPLACEFILE_WRITE_THROUGH,
                         nullptr,
                         nullptr)) {
            return {};
        }
        const DWORD replaceError = GetLastError();
        return std::unexpected(L"Unable to replace configuration file '" +
                               pathForError(destination) + L"': " +
                               lastErrorMessage(replaceError));
    }

    if (MoveFileExW(temporary.c_str(),
                    destination.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return {};
    }
    const DWORD moveError = GetLastError();
    return std::unexpected(L"Unable to install configuration file '" +
                           pathForError(destination) + L"': " +
                           lastErrorMessage(moveError));
}

class TemporaryPathGuard final {
public:
    explicit TemporaryPathGuard(std::filesystem::path path) : mPath(std::move(path)) {}

    ~TemporaryPathGuard() noexcept {
        if (!mReleased) {
            std::error_code error;
            std::filesystem::remove(mPath, error);
        }
    }

    void release() noexcept { mReleased = true; }

private:
    std::filesystem::path mPath;
    bool mReleased{false};
};

}  // namespace

ConfigStore::ConfigStore(std::filesystem::path path) : mPath(std::move(path)) {}

std::filesystem::path ConfigStore::defaultConfigPath() {
    return localAppDataPath() / L"CommandRunner" / L"commands.json";
}

ConfigLoadResult ConfigStore::load() const {
    try {
        std::error_code existsError;
        const bool exists = std::filesystem::exists(mPath, existsError);
        if (existsError) {
            return std::unexpected(errorForPath(mPath, L"configuration path could not be checked"));
        }
        if (!exists) {
            return ConfigData{};
        }

        std::ifstream input(mPath, std::ios::binary);
        if (!input) {
            return std::unexpected(errorForPath(mPath, L"file could not be opened"));
        }
        std::stringstream contents;
        contents << input.rdbuf();
        if (!input.good() && !input.eof()) {
            return std::unexpected(errorForPath(mPath, L"file could not be read"));
        }

        const json payload = json::parse(contents.str(), nullptr, false);
        if (payload.is_discarded()) {
            return std::unexpected(errorForPath(mPath, L"invalid JSON"));
        }
        if (!payload.is_object()) {
            return std::unexpected(errorForPath(mPath,
                                                L"top-level configuration must be an object"));
        }

        ConfigData data;
        const auto commandsIterator = payload.find("commands");
        if (commandsIterator != payload.end()) {
            if (!commandsIterator->is_array()) {
                return std::unexpected(errorForPath(mPath, L"field 'commands' must be an array"));
            }
            for (const auto& item : *commandsIterator) {
                const auto command = commandFromJson(item);
                if (!command) {
                    return std::unexpected(errorForPath(mPath,
                                                        L"invalid command: " + command.error()));
                }
                data.mCommands.push_back(*command);
            }
        }

        const auto preferencesIterator = payload.find("preferences");
        if (preferencesIterator != payload.end()) {
            if (!preferencesIterator->is_object()) {
                return std::unexpected(errorForPath(mPath,
                                                    L"field 'preferences' must be an object"));
            }
            const auto wrapLines = optionalBool(*preferencesIterator, "wrap_lines", false);
            if (!wrapLines) {
                return std::unexpected(errorForPath(mPath, wrapLines.error()));
            }
            const auto autoScroll = optionalBool(*preferencesIterator, "auto_scroll", true);
            if (!autoScroll) {
                return std::unexpected(errorForPath(mPath, autoScroll.error()));
            }
            data.mPreferences.mWrapLines = *wrapLines;
            data.mPreferences.mAutoScroll = *autoScroll;
        }
        return data;
    } catch (const std::exception& exception) {
        return std::unexpected(errorForPath(mPath, exceptionMessage(exception)));
    } catch (...) {
        return std::unexpected(errorForPath(mPath, L"unexpected error"));
    }
}

ConfigSaveResult ConfigStore::save(const ConfigData& data) const {
    std::filesystem::path temporaryPath;
    try {
        temporaryPath = mPath;
        temporaryPath += L".tmp";
        TemporaryPathGuard temporaryFile(temporaryPath);

        const auto parent = mPath.parent_path();
        if (!parent.empty()) {
            std::error_code directoryError;
            std::filesystem::create_directories(parent, directoryError);
            if (directoryError) {
                return std::unexpected(errorForPath(
                    mPath, L"parent directory could not be created"));
            }
        }

        const auto payload = dataToJson(data);
        if (!payload) {
            return std::unexpected(errorForPath(mPath, payload.error()));
        }

        std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!output) {
            return std::unexpected(errorForPath(
                temporaryPath, L"temporary file could not be opened"));
        }
        output << payload->dump(2) << '\n';
        output.flush();
        if (!output) {
            return std::unexpected(errorForPath(
                temporaryPath, L"temporary file could not be written"));
        }
        output.close();
        if (!output) {
            return std::unexpected(errorForPath(
                temporaryPath, L"temporary file could not be closed"));
        }

        const auto replacement = replaceAtomically(temporaryPath, mPath);
        if (!replacement) {
            return std::unexpected(replacement.error());
        }
        temporaryFile.release();
        return {};
    } catch (const std::exception& exception) {
        return std::unexpected(errorForPath(mPath, exceptionMessage(exception)));
    } catch (...) {
        return std::unexpected(errorForPath(mPath, L"unexpected error"));
    }
}

ConfigSaveResult ConfigStore::save(std::span<const CommandConfig> commands,
                                   const Preferences& preferences) const {
    ConfigData data;
    data.mCommands.assign(commands.begin(), commands.end());
    data.mPreferences = preferences;
    return save(data);
}

}  // namespace command_runner
