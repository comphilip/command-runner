#pragma once

#include "core/models.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace command_runner {

struct ConfigData {
    std::vector<CommandConfig> commands;
    Preferences preferences;

    friend bool operator==(const ConfigData&, const ConfigData&) = default;
};

struct ConfigLoadResult {
    ConfigData data;
    std::optional<std::wstring> error;

    [[nodiscard]] bool succeeded() const noexcept { return !error.has_value(); }
};

class ConfigStore {
public:
    explicit ConfigStore(std::filesystem::path path = default_config_path());

    [[nodiscard]] static std::filesystem::path default_config_path();
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

    // A failed load returns safe defaults and an English diagnostic. It never
    // changes the file, allowing the UI to report Configuration Error safely.
    [[nodiscard]] ConfigLoadResult load() const noexcept;

    // Writes beside the destination and replaces it only after the new JSON
    // is fully flushed. The existing file is left untouched on failure.
    [[nodiscard]] bool save(const ConfigData& data, std::wstring* error = nullptr) const noexcept;
    [[nodiscard]] bool save(const std::vector<CommandConfig>& commands,
                            const Preferences& preferences,
                            std::wstring* error = nullptr) const noexcept;

private:
    std::filesystem::path path_;
};

}  // namespace command_runner
