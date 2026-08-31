#pragma once

#include "core/models.h"

#include <filesystem>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace command_runner {

struct ConfigData {
    std::vector<CommandConfig> mCommands;
    Preferences mPreferences;

    friend bool operator==(const ConfigData&, const ConfigData&) = default;
};

using ConfigLoadResult = std::expected<ConfigData, std::wstring>;
using ConfigSaveResult = std::expected<void, std::wstring>;

class ConfigStore {
public:
    explicit ConfigStore(std::filesystem::path path = defaultConfigPath());

    [[nodiscard]] static std::filesystem::path defaultConfigPath();
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return mPath; }

    // A failed load returns an English diagnostic and never changes the file.
    // Callers can use ConfigData{} when they need safe defaults.
    [[nodiscard]] ConfigLoadResult load() const;

    // Writes beside the destination and replaces it only after the new JSON
    // is fully flushed. The existing file is left untouched on failure.
    [[nodiscard]] ConfigSaveResult save(const ConfigData& data) const;
    [[nodiscard]] ConfigSaveResult save(std::span<const CommandConfig> commands,
                                        const Preferences& preferences) const;

private:
    std::filesystem::path mPath;
};

}  // namespace command_runner
