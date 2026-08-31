#include "core/config_store.h"

#include <windows.h>
#include <shlobj.h>

#include <wil/resource.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace command_runner {
namespace {

using json = nlohmann::json;

std::wstring utf8_to_wide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         value.data(), static_cast<int>(value.size()),
                                         nullptr, 0);
    if (size <= 0) {
        throw std::runtime_error("invalid UTF-8");
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                            value.data(), static_cast<int>(value.size()),
                            result.data(), size) != size) {
        throw std::runtime_error("invalid UTF-8");
    }
    return result;
}

std::string wide_to_utf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                         value.data(), static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        throw std::runtime_error("invalid UTF-16");
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                            value.data(), static_cast<int>(value.size()),
                            result.data(), size, nullptr, nullptr) != size) {
        throw std::runtime_error("invalid UTF-16");
    }
    return result;
}

std::wstring last_error_message(DWORD error) {
    wchar_t* buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
                        FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length = FormatMessageW(flags, nullptr, error, 0,
                                        reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring message = length != 0 ? std::wstring(buffer, length)
                                      : L"Unknown Windows error";
    if (buffer != nullptr) {
        LocalFree(buffer);
    }
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n')) {
        message.pop_back();
    }
    return message;
}

std::wstring path_for_error(const std::filesystem::path& path) {
    return path.empty() ? L"<empty path>" : path.wstring();
}

std::wstring exception_message(const std::exception& exception) {
    try {
        return utf8_to_wide(exception.what());
    } catch (...) {
        return L"Unexpected configuration error";
    }
}

template <typename T>
T required_value(const json& object, const char* name) {
    const auto iterator = object.find(name);
    if (iterator == object.end()) {
        throw std::invalid_argument(std::string("missing required field '") + name + "'");
    }
    if (!iterator->is_string()) {
        throw std::invalid_argument(std::string("field '") + name + "' must be a string");
    }
    return utf8_to_wide(iterator->get<std::string>());
}

std::wstring optional_string(const json& object, const char* name, std::wstring default_value = {}) {
    const auto iterator = object.find(name);
    if (iterator == object.end()) {
        return default_value;
    }
    if (!iterator->is_string()) {
        throw std::invalid_argument(std::string("field '") + name + "' must be a string");
    }
    return utf8_to_wide(iterator->get<std::string>());
}

std::string optional_encoding(const json& object) {
    const auto iterator = object.find("encoding");
    if (iterator == object.end()) {
        return "auto";
    }
    if (!iterator->is_string()) {
        throw std::invalid_argument("field 'encoding' must be a string");
    }
    return iterator->get<std::string>();
}

bool optional_bool(const json& object, const char* name, bool default_value) {
    const auto iterator = object.find(name);
    if (iterator == object.end()) {
        return default_value;
    }
    if (!iterator->is_boolean()) {
        throw std::invalid_argument(std::string("field '") + name + "' must be a boolean");
    }
    return iterator->get<bool>();
}

CommandConfig command_from_json(const json& object) {
    if (!object.is_object()) {
        throw std::invalid_argument("each item in 'commands' must be an object");
    }
    return CommandConfig(
        required_value<std::wstring>(object, "name"),
        required_value<std::wstring>(object, "working_directory"),
        required_value<std::wstring>(object, "command_line"),
        optional_encoding(object),
        optional_string(object, "id"),
        optional_bool(object, "auto_start", false),
        optional_bool(object, "shell", false));
}

json command_to_json(const CommandConfig& command) {
    return json{
        {"id", wide_to_utf8(command.id)},
        {"name", wide_to_utf8(command.name)},
        {"working_directory", wide_to_utf8(command.working_directory)},
        {"command_line", wide_to_utf8(command.command_line)},
        {"encoding", command.encoding},
        {"auto_start", command.auto_start},
        {"shell", command.shell},
    };
}

json data_to_json(const ConfigData& data) {
    json commands = json::array();
    for (const auto& command : data.commands) {
        commands.push_back(command_to_json(command));
    }
    return json{
        {"version", 1},
        {"commands", std::move(commands)},
        {"preferences", {
            {"wrap_lines", data.preferences.wrap_lines},
            {"auto_scroll", data.preferences.auto_scroll},
        }},
    };
}

std::filesystem::path local_app_data_path() {
    wchar_t value[32768]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", value,
                                                  static_cast<DWORD>(std::size(value)));
    if (length > 0 && length < std::size(value)) {
        return std::filesystem::path(std::wstring(value, length));
    }

    PWSTR known_folder = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT,
                                       nullptr, &known_folder))) {
        wil::unique_cotaskmem_string owned_folder;
        owned_folder.reset(known_folder);
        return std::filesystem::path(owned_folder.get());
    }

    std::error_code error;
    const auto fallback = std::filesystem::current_path(error);
    return error ? std::filesystem::path(L".") : fallback;
}

void set_error(std::wstring* error, std::wstring value) {
    if (error != nullptr) {
        *error = std::move(value);
    }
}

bool replace_atomically(const std::filesystem::path& temporary,
                        const std::filesystem::path& destination,
                        std::wstring* error) {
    const DWORD attributes = GetFileAttributesW(destination.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        if (ReplaceFileW(destination.c_str(), temporary.c_str(), nullptr,
                          REPLACEFILE_WRITE_THROUGH, nullptr, nullptr)) {
            return true;
        }
        const DWORD replace_error = GetLastError();
        set_error(error, L"Unable to replace configuration file '" +
                           path_for_error(destination) + L"': " +
                           last_error_message(replace_error));
        return false;
    }

    if (MoveFileExW(temporary.c_str(), destination.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return true;
    }
    const DWORD move_error = GetLastError();
    set_error(error, L"Unable to install configuration file '" +
                       path_for_error(destination) + L"': " +
                       last_error_message(move_error));
    return false;
}

}  // namespace

ConfigStore::ConfigStore(std::filesystem::path path) : path_(std::move(path)) {}

std::filesystem::path ConfigStore::default_config_path() {
    return local_app_data_path() / L"CommandRunner" / L"commands.json";
}

ConfigLoadResult ConfigStore::load() const noexcept {
    ConfigLoadResult result;
    try {
        std::error_code exists_error;
        if (!std::filesystem::exists(path_, exists_error)) {
            if (exists_error) {
                throw std::system_error(exists_error,
                                        "configuration path could not be checked");
            }
            return result;
        }

        std::ifstream input(path_, std::ios::binary);
        if (!input) {
            throw std::runtime_error("file could not be opened");
        }
        std::stringstream contents;
        contents << input.rdbuf();
        if (!input.good() && !input.eof()) {
            throw std::runtime_error("file could not be read");
        }

        const json payload = json::parse(contents.str());
        if (!payload.is_object()) {
            throw std::invalid_argument("top-level configuration must be an object");
        }

        const auto commands_iterator = payload.find("commands");
        if (commands_iterator != payload.end()) {
            if (!commands_iterator->is_array()) {
                throw std::invalid_argument("field 'commands' must be an array");
            }
            for (const auto& item : *commands_iterator) {
                result.data.commands.push_back(command_from_json(item));
            }
        }

        const auto preferences_iterator = payload.find("preferences");
        if (preferences_iterator != payload.end()) {
            if (!preferences_iterator->is_object()) {
                throw std::invalid_argument("field 'preferences' must be an object");
            }
            result.data.preferences.wrap_lines = optional_bool(
                *preferences_iterator, "wrap_lines", false);
            result.data.preferences.auto_scroll = optional_bool(
                *preferences_iterator, "auto_scroll", true);
        }
    } catch (const std::exception& exception) {
        result.data = {};
        result.error = L"Unable to read configuration file '" + path_for_error(path_) +
                       L"': " + exception_message(exception);
    } catch (...) {
        result.data = {};
        result.error = L"Unable to read configuration file '" + path_for_error(path_) +
                       L"': unexpected error";
    }
    return result;
}

bool ConfigStore::save(const ConfigData& data, std::wstring* error) const noexcept {
    if (error != nullptr) {
        error->clear();
    }

    std::filesystem::path temporary_path;
    try {
        temporary_path = path_;
        temporary_path += L".tmp";
        const auto parent = path_.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent);
        }

        std::ofstream output(temporary_path, std::ios::binary | std::ios::trunc);
        if (!output) {
            set_error(error, L"Unable to open temporary configuration file '" +
                               path_for_error(temporary_path) + L"'");
            return false;
        }
        output << data_to_json(data).dump(2) << '\n';
        output.flush();
        if (!output) {
            set_error(error, L"Unable to write temporary configuration file '" +
                               path_for_error(temporary_path) + L"'");
            output.close();
            std::error_code ignored;
            std::filesystem::remove(temporary_path, ignored);
            return false;
        }
        output.close();

        if (!replace_atomically(temporary_path, path_, error)) {
            std::error_code ignored;
            std::filesystem::remove(temporary_path, ignored);
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        set_error(error, L"Unable to save configuration file '" + path_for_error(path_) +
                           L"': " + exception_message(exception));
    } catch (...) {
        set_error(error, L"Unable to save configuration file '" + path_for_error(path_) +
                           L"': unexpected error");
    }

    std::error_code ignored;
    std::filesystem::remove(temporary_path, ignored);
    return false;
}

bool ConfigStore::save(const std::vector<CommandConfig>& commands,
                       const Preferences& preferences,
                       std::wstring* error) const noexcept {
    return save(ConfigData{commands, preferences}, error);
}

}  // namespace command_runner
