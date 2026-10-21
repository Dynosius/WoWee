// Versioned device sections use COMMAND<TAB>KEY1<TAB>KEY2, including empty slots.
// Unsectioned master files use COMMAND=KEY1,KEY2 (or the old tab variant).
#pragma once

#include <array>
#include <istream>
#include <map>
#include <set>
#include <string>

namespace wowee::addons {

using SavedBindings = std::map<std::string, std::array<std::string, 2>>;
inline constexpr const char* bindingFileVersion = "# wowee-bindings-v2";
inline constexpr const char* nativeSettingsMarker = "# wowee-native-settings-merged-v1";

struct SavedBindingFile {
    SavedBindings keyboardMouse;
    SavedBindings controller;
    std::set<std::string> implicitSecondary; // master's writer emitted a trailing comma
    bool versioned = false;
    bool nativeSettingsMerged = false;
    bool valid = true;
};

inline SavedBindingFile parseBindingFile(std::istream& in) {
    SavedBindingFile out;
    SavedBindings* device = nullptr;
    std::string line;
    bool sawRow = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line == bindingFileVersion && !sawRow) { out.versioned = true; continue; }
        if (line == nativeSettingsMarker) { out.nativeSettingsMerged = true; continue; }
        const bool comment = line.find(out.versioned ? '\t' : '=') == std::string::npos && (line[0] == '#' || line[0] == ';');
        if (comment && line.starts_with("# wowee-bindings-")) { out.valid = false; continue; }
        if (comment) continue;
        if (out.versioned && line[0] == '[' && line.find('\t') == std::string::npos) {
            if (line == "[KeyboardMouse]") device = &out.keyboardMouse;
            else if (line == "[Controller]") device = &out.controller;
            else { device = nullptr; out.valid = false; }
            continue;
        }
        sawRow = true;
        const size_t eq = line.find(out.versioned ? '\t' : '=');
        if (eq == std::string::npos || eq == 0 || (out.versioned && !device)) {
            out.valid = false;
            continue;
        }
        const auto command = line.substr(0, eq);
        const auto rest = line.substr(eq + 1);
        const size_t tab = rest.find('\t');
        size_t separator = out.versioned || tab != std::string::npos ? tab : rest.find(',');
        // In master output a comma key itself precedes the comma separator.
        if (!out.versioned && tab == std::string::npos && separator != std::string::npos &&
            separator + 1 < rest.size() && rest[separator + 1] == ',' &&
            (separator == 0 || rest[separator - 1] == '-')) ++separator;
        if (out.versioned && (separator == std::string::npos || rest.find('\t', separator + 1) != std::string::npos)) {
            out.valid = false;
            continue;
        }
        auto& rows = out.versioned ? *device : out.keyboardMouse;
        rows[command] = {rest.substr(0, separator), separator == std::string::npos ? "" : rest.substr(separator + 1)};
        if (!out.versioned) {
            out.implicitSecondary.erase(command);
            if (tab == std::string::npos && separator != std::string::npos && separator + 1 == rest.size() && separator != 0)
                out.implicitSecondary.insert(command);
        }
    }
    if (in.bad()) out.valid = false;
    return out;
}

// Legacy convenience query; device-aware consumers use parseBindingFile().
inline SavedBindings parseSavedBindings(std::istream& in) {
    return parseBindingFile(in).keyboardMouse;
}

} // namespace wowee::addons
