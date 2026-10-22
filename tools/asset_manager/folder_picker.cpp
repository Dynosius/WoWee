/// The parts of choosing that are the same everywhere: the in-window browser,
/// and deciding which chooser to use.

#include "folder_picker.hpp"

#include <algorithm>
#include <filesystem>
#include <cstdlib>

#include "ui/paper_ui.hpp"

namespace wowee::assets {
namespace {

namespace fs = std::filesystem;

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

/// Somewhere that exists, to start at when what was asked for does not.
std::string somewhereReal(const std::string& wanted) {
    std::error_code ec;
    if (!wanted.empty() && fs::is_directory(wanted, ec)) return wanted;
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') return home;
    if (const char* profile = std::getenv("USERPROFILE"); profile != nullptr && *profile != '\0') {
        return profile;
    }
    return fs::current_path(ec).string();
}

}  // namespace

void Browser::open(PickWhat what, std::string title, const std::string& startAt,
                   std::string extension) {
    what_ = what;
    title_ = std::move(title);
    extension_ = lower(std::move(extension));
    here_ = somewhereReal(startAt);
    selected_ = -1;
    open_ = true;
    listHere();
}

void Browser::listHere() {
    entries_.clear();
    error_.clear();
    selected_ = -1;

    std::error_code ec;
    for (fs::directory_iterator it(here_, ec), end; it != end && !ec; it.increment(ec)) {
        const bool directory = it->is_directory(ec);
        std::string name = it->path().filename().string();
        // Dot files are almost never what is being looked for here and there
        // are a great many of them in a home directory.
        if (!name.empty() && name[0] == '.') continue;
        if (!directory) {
            if (what_ == PickWhat::Folder) continue;
            if (!extension_.empty() && lower(it->path().extension().string()) != extension_) {
                continue;
            }
        }
        entries_.push_back({std::move(name), directory});
    }
    if (ec) error_ = "Cannot read this folder.";

    // Folders first, then names, so the shape of the place is readable.
    std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
        if (a.directory != b.directory) return a.directory;
        return lower(a.name) < lower(b.name);
    });
}

bool Browser::draw(ui::PaperUI& ui, std::string* chosen) {
    if (!open_) return false;

    // Over everything, on a dimmed page: a card of its own in the middle of
    // the window, sized to it so a deep folder has room to be read.
    ui.setLayer(ui::PaperLayer::Overlay);
    ui.scrim(0.35f);
    const ImVec2 screen = ui.screenSize();
    const ImVec2 a(screen.x * 0.1f, screen.y * 0.13f);
    const ImVec2 b(screen.x * 0.9f, screen.y * 0.87f);
    ui.sheet(a, b, false);
    ui.claimMouse(a, b);

    const ui::PaperTheme& theme = ui.theme();
    const float pad = ui.px(30);
    const float x0 = a.x + pad;
    const float x1 = b.x - pad;
    float y = a.y + ui.px(24);

    const float titleSize = ui.px(21);
    ui.text(ImVec2(x0, y), title_.c_str(), titleSize, theme.ink, true);
    y += ui.inkHeight(titleSize, true) + ui.px(10);

    const float row = ui.px(32);
    const fs::path up = fs::path(here_).parent_path();
    const bool canGoUp = !up.empty() && up != fs::path(here_);
    const float upW = ui.textWidth("Up", row * 0.44f) + ui.px(40);
    if (ui.button("browser.up", ImVec2(x0, y), ImVec2(x0 + upW, y + row), "Up",
                  ui::PaperUI::ButtonKind::Quiet, canGoUp)) {
        here_ = up.string();
        listHere();
    }
    // Where this is, beside the way out of it, and what went wrong reading it.
    const float pathSize = ui.px(14);
    const std::string where = error_.empty() ? here_ : here_ + "   - " + error_;
    ui.text(ImVec2(x0 + upW + ui.px(12), y + (row - pathSize) * 0.5f - ui.px(1)), where.c_str(),
            pathSize, error_.empty() ? theme.inkSoft : theme.crayonRed);
    y += row + ui.px(10);

    bool decided = false;
    const float listBottom = b.y - pad - row - ui.px(12);
    const float entrySize = ui.px(15);
    const ui::PaperUI::ListResult picked = ui.list(
        "browser.entries", ImVec2(x0, y), ImVec2(x1, listBottom),
        static_cast<int>(entries_.size()), ui.px(26), selected_,
        [&](int i, ImVec2 ra, ImVec2 rb, bool, bool) {
            const Entry& entry = entries_[static_cast<std::size_t>(i)];
            const std::string label = entry.directory ? entry.name + "/" : entry.name;
            ui.text(ImVec2(ra.x + ui.px(10), ra.y + ((rb.y - ra.y) - entrySize) * 0.5f - ui.px(1)),
                    label.c_str(), entrySize, entry.directory ? theme.ink : theme.inkSoft);
        });
    if (picked.clicked >= 0) selected_ = picked.clicked;
    if (picked.activated >= 0) {
        const Entry& entry = entries_[static_cast<std::size_t>(picked.activated)];
        if (entry.directory) {
            here_ = (fs::path(here_) / entry.name).string();
            listHere();
        } else {
            if (chosen != nullptr) *chosen = (fs::path(here_) / entry.name).string();
            decided = true;
        }
    }

    // A folder is chosen by being in it, which is how every system chooser
    // does it too: there is no selecting the folder you are looking at from
    // a list that does not contain it.
    const bool canChoose =
        what_ == PickWhat::Folder ||
        (selected_ >= 0 && selected_ < int(entries_.size()) && !entries_[selected_].directory);
    const char* verb = what_ == PickWhat::Folder ? "Use this folder" : "Open";
    const float by = b.y - pad - row;
    const float verbW = ui.textWidth(verb, row * 0.44f) + ui.px(48);
    const float cancelW = ui.textWidth("Cancel", row * 0.44f) + ui.px(48);
    if (ui.button("browser.choose", ImVec2(x1 - verbW, by), ImVec2(x1, by + row), verb,
                  ui::PaperUI::ButtonKind::Primary, canChoose) && !decided) {
        if (chosen != nullptr) {
            *chosen = what_ == PickWhat::Folder
                          ? here_
                          : (fs::path(here_) / entries_[selected_].name).string();
        }
        decided = true;
    }
    if (ui.button("browser.cancel", ImVec2(x1 - verbW - ui.px(12) - cancelW, by),
                  ImVec2(x1 - verbW - ui.px(12), by + row), "Cancel")) {
        open_ = false;
    }

    ui.setLayer(ui::PaperLayer::Page);
    if (decided) open_ = false;
    return decided;
}

bool Picker::ask(PickWhat what, const std::string& title, const std::string& startAt,
                 const std::string& extension, std::string* chosen) {
    if (haveNativePicker() && pickNative(what, title, startAt, extension, chosen)) {
        return true;
    }
    // Either there is no system chooser here, or it was cancelled. Cancelling is
    // not a reason to open a second one, so the browser is only reached where
    // there was nothing to cancel.
    if (!haveNativePicker()) {
        browser_.open(what, title, startAt, extension);
    }
    return false;
}

}  // namespace wowee::assets
