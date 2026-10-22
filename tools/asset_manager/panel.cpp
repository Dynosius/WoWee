#include "panel.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "core/data_paths.hpp"
#include "core/local_time.hpp"
#include "pipeline/asset_inventory.hpp"

#include "casc.hpp"
#include "pack.hpp"
#include "profiles.hpp"

namespace wowee::assets {
namespace {

namespace fs = std::filesystem;
using ui::PaperUI;

// Sizes, in the units PaperUI scales by. The client lays its login card out
// in the same units, so the panel is drawn at the size of what is around it.
constexpr float kText = 15.0f;
constexpr float kSmall = 13.5f;
constexpr float kHeading = 21.0f;
constexpr float kRow = 32.0f;
constexpr float kBox = 17.0f;
constexpr float kIndent = 27.0f;
/// The log pane, once a build is running.
constexpr float kLogHeight = 190.0f;
/// The questions' room for their scroll mark, so it never sits on a field.
constexpr float kMarkRoom = 16.0f;

/// The page being written down, one row under the last.
struct Page {
    PaperUI& ui;
    ui::Column col;

    [[nodiscard]] float px(float units) const { return ui.px(units); }
    [[nodiscard]] const ui::PaperTheme& theme() const { return ui.theme(); }
};

/// A paragraph at the running y, `indent` units in.
void say(Page& p, const std::string& text, ImU32 colour, float size = kText,
         float indent = 0.0f) {
    if (text.empty()) return;
    const float in = p.px(indent);
    const float h = p.ui.wrapped(ImVec2(p.col.x0 + in, p.col.y), p.col.width() - in,
                                 text.c_str(), p.px(size), colour);
    p.col.gap(h);
}

/// The quieter tone, for what explains rather than asks.
void note(Page& p, const std::string& text, float indent = 0.0f) {
    say(p, text, p.theme().inkSoft, kSmall, indent);
}

/// A numbered question, in the title face with a stroke under it - the way
/// the login card heads itself.
void heading(Page& p, const char* text) {
    p.col.gap(p.px(16));
    const float size = p.px(kHeading);
    p.ui.text(ImVec2(p.col.x0, p.col.y), text, size, p.theme().ink, true);
    const float under = p.col.y + p.ui.inkHeight(size, true) + p.px(2);
    const float wide = std::min(p.ui.textWidth(text, size, true) + p.px(12), p.col.width());
    p.ui.squiggle(ImVec2(p.col.x0, under), ImVec2(p.col.x0 + wide, under),
                  ui::paperFade(p.theme().crayonRed, 0.85f), p.px(1.4f),
                  static_cast<uint32_t>(std::hash<std::string>{}(text)));
    p.col.y = under + p.px(12);
}

/// Actually open a CASC install, on demand.
///
/// Not while somebody is typing: reading its encoding table is around a second
/// of work and this happens on the frame a button is pressed, not on the frame
/// a character is entered.
void confirmSecond(App& app) {
    if (app.secondScan.kind != InstallKind::Casc) return;
    app.cascConfirmed = confirmCasc(app.secondScan, &app.cascError);
}

bool haveGame(const App& app)   { return app.gameScan.kind == InstallKind::Mpq; }
bool haveBorrow(const App& app) { return app.secondScan.kind == InstallKind::Mpq; }
bool haveLater(const App& app)  { return app.secondScan.kind == InstallKind::Casc; }

/// One row: a path, a Browse button that opens the system's own chooser, and
/// whatever the scan made of what is there.
///
/// The button is what people look for. Typing a path still works and dropping a
/// folder on the window still works, but neither is discoverable, and a text
/// box with no button beside it reads as the only way in.
void folderRow(App& app, Page& p, const char* id, const char* label, ui::TextEdit& path,
               PickWhat what, const char* title, int pickId) {
    say(p, label, p.theme().ink);
    p.col.gap(p.px(4));

    const float h = p.px(kRow);
    const char* browse = "Browse...";
    const float browseW = p.ui.textWidth(browse, h * 0.44f) + p.px(36);
    const ImVec2 fa(p.col.x0, p.col.y);
    const ImVec2 fb(p.col.x1 - browseW - p.px(8), p.col.y + h);
    const std::string fieldId = std::string(id) + ".path";
    if (p.ui.field(fieldId.c_str(), fa, fb, path).changed) rescan(app);

    const std::string buttonId = std::string(id) + ".browse";
    if (p.ui.button(buttonId.c_str(), ImVec2(fb.x + p.px(8), fa.y), ImVec2(p.col.x1, fb.y),
                    browse)) {
        std::string chosen;
        if (app.picker.ask(what, title, path.text(), "", &chosen)) {
            path.setText(chosen);
            rescan(app);
        } else {
            app.pendingPick = pickId;   // no system chooser here; the in-window one opened
        }
    }
    p.col.gap(h + p.px(6));
}

/// The games already extracted into a folder, newest-looking first.
///
/// Read fresh rather than remembered: a build finishing, a pack being
/// installed and somebody choosing a different folder all change it, and a
/// cached answer is one that goes stale in each of those.
std::vector<std::string> installedGames(const std::string& dataRoot) {
    std::vector<std::string> out;
    // The same reading the client takes of the same folder, so the two cannot
    // describe it differently. An expansion built by something else, or by a
    // later version of this, still counts - it is there and the client will
    // offer it.
    for (const wowee::pipeline::AssetSet& set : wowee::pipeline::takeInventory(dataRoot).sets) {
        if (set.usable()) out.push_back(set.name);
    }
    return out;
}

/// What is in the destination, in as much detail as there is.
std::vector<std::string> installedDetail(const std::string& dataRoot) {
    std::vector<std::string> out;
    for (const wowee::pipeline::AssetSet& set : wowee::pipeline::takeInventory(dataRoot).sets) {
        // A set nobody has built is a directory of protocol definitions this
        // client ships, not something to report as present or as a problem.
        if (set.usable() || set.anyAssets) out.push_back(set.summary());
    }
    return out;
}

void drawGame(App& app, Page& p) {
    heading(p, "1.  Which game's assets do you want to use?");
    // Named for what is being chosen rather than for what this program then
    // does with it. "The folder you want to build from" describes the build;
    // somebody deciding which of two installations to point at is choosing
    // whose art and sounds they are going to be looking at.
    folderRow(app, p, "game", "The World of Warcraft installation to take them from",
              app.gameDir, PickWhat::Folder,
              "Choose the World of Warcraft installation to use", 1);

    if (!app.gameScan.note.empty()) {
        say(p, app.gameScan.note,
            haveGame(app) ? p.theme().crayonGreen : p.theme().crayonRedDim);
    } else {
        note(p, "You can also drag the folder onto this window.");
    }

    p.col.gap(p.px(10));
    folderRow(app, p, "output", "Where the assets should go", app.outputDir, PickWhat::Folder,
              "Choose where to put the built assets", 4);

    const std::vector<std::string> detail = installedDetail(app.outputDir.text());
    if (detail.empty()) {
        note(p, "Nothing built here yet. This is where the client looks for its assets.");
        return;
    }
    // Several games can share one folder, each under its own name, and the
    // client picks between them at its login screen. Saying what is already
    // there is what makes that visible: otherwise a second game built into
    // the same place looks like it overwrote the first.
    say(p, "Already here:", p.theme().crayonGreen);
    for (const std::string& line : detail) note(p, line, kIndent);
    if (installedGames(app.outputDir.text()).size() < 2) {
        note(p, "Build another game into this same folder and you can choose between "
                "them when the client starts.");
    } else {
        note(p, "The client offers all of them at its login screen.");
    }
}

void drawBase(App& app, Page& p) {
    heading(p, "2.  Which game does your server run?");

    if (!haveGame(app)) {
        note(p, "Choose the installation above first.");
        return;
    }

    // Whatever they pointed at, chosen for them. They can still say otherwise -
    // a 3.3.5 server can be played with a Cataclysm client's art - but the
    // common case is that the folder they just picked is the answer.
    if (!app.detected && !app.gameScan.expansion.empty()) {
        app.selectedBase = app.gameScan.expansion;
        app.detected = true;
    }

    for (const Base& base : bases()) {
        std::string label = base.name + "   " + base.patch;
        if (base.id == app.gameScan.expansion) label += "     (this is what you pointed at)";
        const std::string id = "base." + base.id;
        if (p.ui.radio(id.c_str(), ImVec2(p.col.x0, p.col.y), p.px(kBox), label.c_str(),
                       app.selectedBase == base.id)) {
            app.selectedBase = base.id;
        }
        p.col.gap(p.px(kBox) + p.px(3));
        note(p, base.detail, kIndent);
        p.col.gap(p.px(6));
    }
}

void drawUpgrades(App& app, Page& p) {
    const Base* base = baseById(app.selectedBase);
    if (base == nullptr || !haveGame(app)) return;

    heading(p, "3.  Do you want updated assets?");
    note(p, "Optional. Everything here is on top of the game above, and each one can "
            "be left off.");
    p.col.gap(p.px(8));

    // Two upgrades can want the same second installation, and asking for it
    // once per upgrade puts the same field and the same warning on screen twice
    // - which reads as two different things being needed.
    Source asked = Source::None;

    for (const Upgrade& upgrade : upgrades()) {
        if (!upgradeSuitsBase(upgrade, *base)) continue;

        std::string why;
        const bool usable = sourceAvailable(upgrade.source, haveGame(app), haveBorrow(app),
                                            haveLater(app), &why);
        auto at = std::find(app.chosen.begin(), app.chosen.end(), upgrade.id);
        bool ticked = at != app.chosen.end();

        const std::string id = "upgrade." + upgrade.id;
        if (p.ui.checkbox(id.c_str(), ImVec2(p.col.x0, p.col.y), p.px(kBox),
                          upgrade.name.c_str(), &ticked, usable)) {
            if (ticked) app.chosen.push_back(upgrade.id);
            else app.chosen.erase(at);
        }
        p.col.gap(p.px(kBox) + p.px(3));
        note(p, upgrade.summary, kIndent);

        // The installation an upgrade needs, asked for beside it - once per
        // installation, and whether or not it is already set. Hidden as soon as
        // it is valid, there would be no way to see which one is being used or
        // to point at a different one.
        if (upgrade.source != Source::None && upgrade.source != asked) {
            asked = upgrade.source;
            Page inner{p.ui, ui::Column{p.col.x0 + p.px(kIndent), p.col.x1, p.col.y}};
            inner.col.gap(p.px(4));
            if (!usable) say(inner, why, p.theme().crayonRedDim);
            folderRow(app, inner, "second", upgrade.source == Source::Later
                                                ? "Your Legion (or later) installation"
                                                : "Your Cataclysm installation",
                      app.secondDir, PickWhat::Folder,
                      "Choose the installation to take art from", 2);
            if (!app.secondScan.note.empty()) note(inner, app.secondScan.note);

            if (usable && upgrade.source == Source::Later && !app.cascConfirmed) {
                const float h = p.px(kRow);
                const char* label = "Check it";
                const float w = p.ui.textWidth(label, h * 0.44f) + p.px(36);
                if (p.ui.button("second.check", ImVec2(inner.col.x0, inner.col.y),
                                ImVec2(inner.col.x0 + w, inner.col.y + h), label)) {
                    confirmSecond(app);
                }
                const float size = p.px(kSmall);
                p.ui.text(ImVec2(inner.col.x0 + w + p.px(12), inner.col.y + (h - size) * 0.5f),
                          "Reads its file table - about a second.", size,
                          p.theme().inkSoft);
                inner.col.gap(h + p.px(6));
            }
            if (!app.cascError.empty() && upgrade.source == Source::Later) {
                say(inner, app.cascError, p.theme().crayonRed);
            }
            p.col.y = inner.col.y;
        } else if (upgrade.source != Source::None) {
            note(p, upgrade.source == Source::Later
                        ? "Uses the same Legion installation as above."
                        : "Uses the same Cataclysm installation as above.",
                 kIndent);
        }
        p.col.gap(p.px(8));
    }
}

void drawPlan(App& app, Page& p) {
    const Base* base = baseById(app.selectedBase);
    if (base == nullptr || !haveGame(app)) return;

    const Profile profile = assemble(*base, app.chosen);
    heading(p, "4.  What will happen");

    const std::vector<JobStage> planned =
        Job::plan(profile, haveBorrow(app), haveLater(app));
    for (std::size_t i = 0; i < planned.size(); ++i) {
        const JobStage& stage = planned[i];
        std::string line = std::to_string(i + 1) + ". " + stage.label;
        if (stage.skipped) {
            note(p, line + "   - skipped, " + stage.skipReason, 8.0f);
        } else {
            say(p, line, p.theme().ink, kText, 8.0f);
        }
    }
    p.col.gap(p.px(6));
    note(p, "About " + std::to_string(profile.minutes()) +
                " minutes. Nothing is written into your game install.");
}

void startPack(App& app, const std::string& profileId) {
    std::string out = app.outputDir.text();
    if (out.empty()) out = (fs::current_path() / "Data").string();

    // A pack holds whatever is in the folder, which may be several games built
    // one after another. Named after the last thing built, a pack of four games
    // says it is one - and the person receiving it has no way to tell.
    const std::vector<std::string> inside = installedGames(out);
    const std::string name = inside.size() > 1 ? "universal" : profileId;

    const std::tm local = wowee::core::localTime(std::time(nullptr));
    char stamp[16];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d", &local);
    const std::string dest =
        (fs::path(out).parent_path() / ("wowee-" + name + "-" + stamp + ".zip")).string();

    app.packing.store(true);
    app.packCancel.store(false);
    {
        std::lock_guard<std::mutex> lock(app.packMutex);
        app.packNote = "Packing...";
    }
    // Joined, not detached. The button above is disabled while either worker
    // runs, so anything still joinable here has already finished.
    if (app.packThread.joinable()) app.packThread.join();
    app.packThread = std::thread([&app, out, dest, name]() {
        PackResult result = writePack(
            out, dest, name,
            [&app](std::size_t done, std::size_t total) {
                std::lock_guard<std::mutex> lock(app.packMutex);
                app.packNote = "Packing " + std::to_string(done) + " of " +
                               std::to_string(total) + " files...";
            },
            app.packCancel);
        std::lock_guard<std::mutex> lock(app.packMutex);
        if (!result.ok) {
            app.packNote = "Could not save: " + result.error;
        } else {
            const double raw = double(result.rawBytes) / (1024.0 * 1024.0 * 1024.0);
            const double packed = double(result.packedBytes) / (1024.0 * 1024.0 * 1024.0);
            char line[512];
            std::snprintf(line, sizeof(line),
                          "Saved %zu files - %.1f GB of assets in a %.1f GB file. %s",
                          result.files, raw, packed, dest.c_str());
            app.packNote = line;
        }
        app.packing.store(false);
    });
}

/// Install a pack somebody else built, into the same tree a build writes to.
void startImport(App& app, const std::string& zipPath) {
    std::string out = app.outputDir.text();
    if (out.empty()) out = (fs::current_path() / "Data").string();

    const PackInfo info = readPackInfo(zipPath);
    if (!info.ok) {
        std::lock_guard<std::mutex> lock(app.packMutex);
        app.packNote = "That does not look like a pack: " + info.error;
        return;
    }

    app.importing.store(true);
    app.packCancel.store(false);
    {
        std::lock_guard<std::mutex> lock(app.packMutex);
        app.packNote = "Installing " + (info.name.empty() ? std::string("a pack") : info.name) +
                       " - " + std::to_string(info.files) + " files...";
    }
    if (app.packThread.joinable()) app.packThread.join();
    app.packThread = std::thread([&app, zipPath, out]() {
        PackResult result = readPack(
            zipPath, out,
            [&app](std::size_t done, std::size_t total) {
                std::lock_guard<std::mutex> lock(app.packMutex);
                app.packNote = "Installing " + std::to_string(done) + " of " +
                               std::to_string(total) + " files...";
            },
            app.packCancel);
        std::lock_guard<std::mutex> lock(app.packMutex);
        if (!result.ok) {
            app.packNote = "Could not install that pack: " + result.error;
        } else {
            const double raw = double(result.rawBytes) / (1024.0 * 1024.0 * 1024.0);
            char line[512];
            std::snprintf(line, sizeof(line), "Installed %zu files - %.1f GB - into %s",
                          result.files, raw, out.c_str());
            app.packNote = line;
        }
        app.importing.store(false);
    });
}

std::string packNoteNow(const App& app) {
    std::lock_guard<std::mutex> lock(app.packMutex);
    return app.packNote;
}

/// How much of the panel the actions need reserved at the bottom.
///
/// The button row always, and the progress bar and log once there is something
/// to watch - which is the moment the log matters most and the questions above
/// matter least. Measured the way drawRun lays them out, so the two agree.
float actionsHeight(const App& app, const PaperUI& ui, float width) {
    float height = ui.px(16) + ui.px(kRow);
    if (const std::string packNote = packNoteNow(app); !packNote.empty()) {
        height += ui.px(6) + ui.wrappedHeight(width, packNote.c_str(), ui.px(kSmall));
    }
    if (app.started) {
        height += ui.px(10) + ui.px(12);                       // the progress bar
        height += ui.px(6) + ui.lineHeight(ui.px(kText));      // what it is doing
        height += ui.px(8) + ui.px(kLogHeight);
    }
    return height;
}

void drawRun(App& app, Page& p) {
    const Base* base = baseById(app.selectedBase);
    const Profile profile = base != nullptr ? assemble(*base, app.chosen) : Profile{};
    const bool busy = app.job.running() || app.packing.load() || app.importing.load();

    const bool ok = base != nullptr &&
                    profileAvailable(profile, haveGame(app), haveBorrow(app),
                                     haveLater(app), nullptr);

    // The row is fitted to the width there is rather than to a set of widths
    // that happen to add up on one window. The numbers below are the
    // proportions the row is divided in, not sizes.
    const bool showStop = app.job.running();
    const float stopWeight = showStop ? 90.0f : 0.0f;
    const float weights = 180.0f + stopWeight + 170.0f + 260.0f;
    const float gap = p.px(10);
    const float gaps = gap * (showStop ? 3.0f : 2.0f);
    const float unit = std::max(0.0f, p.col.width() - gaps) / weights;
    const float top = p.col.y;
    const float bottom = top + p.px(kRow);
    float x = p.col.x0;
    const auto next = [&](float weight) {
        const ImVec2 a(x, top);
        x += weight * unit;
        const ImVec2 b(x, bottom);
        x += gap;
        return std::make_pair(a, b);
    };

    {
        const auto [a, b] = next(180.0f);
        if (p.ui.button("run.build", a, b, "Build my assets", PaperUI::ButtonKind::Primary,
                        ok && !busy)) {
            std::string out = app.outputDir.text();
            if (out.empty()) out = (fs::current_path() / "Data").string();
            // The folder the scan found the archives in, not the one that was
            // typed. They are usually the same and were assumed to be: when they
            // were not - somebody chose the folder that has Data inside it, which
            // is what the field asks for - the panel said "Found a game" from the
            // scan and the extractor then said "No MPQ archives found in" the
            // other path, having been handed the one nobody had checked.
            const auto readFrom = [](const InstallScan& scan, const std::string& typed) {
                return scan.dataDir.empty() ? typed : scan.dataDir;
            };
            app.job.start(profile, readFrom(app.gameScan, app.gameDir.text()),
                          readFrom(app.secondScan, app.secondDir.text()), out,
                          haveBorrow(app), haveLater(app));
            app.started = true;
        }
    }

    // showStop, not a fresh query: the widths above were divided on its
    // value, and a job finishing between there and here would drop the
    // button while its share of the row stayed reserved.
    if (showStop) {
        const auto [a, b] = next(90.0f);
        if (p.ui.button("run.stop", a, b, "Stop")) app.job.cancel();
    }

    // Installing somebody else's pack asks nothing about your game, so it is
    // not behind the questions above: a person handed a pack has no archives to
    // point at and nothing to choose.
    {
        const auto [a, b] = next(170.0f);
        if (p.ui.button("run.install", a, b, "Install a pack...", PaperUI::ButtonKind::Quiet,
                        !busy)) {
            std::string chosen;
            if (app.picker.ask(PickWhat::File, "Choose a pack to install",
                               app.outputDir.text(), ".zip", &chosen)) {
                startImport(app, chosen);
            } else {
                app.pendingPick = 3;
            }
        }
    }

    // Whatever is in the folder, whether this session put it there or a run
    // last week did. Gated on having built something now, a person who came
    // back to send someone their assets found the button dead.
    {
        const std::vector<std::string> inside = installedGames(app.outputDir.text());
        const std::string save = inside.size() > 1
                                     ? "Save all " + std::to_string(inside.size()) +
                                           " as one pack"
                                     : std::string("Save what I have as a pack");
        const auto [a, b] = next(260.0f);
        if (p.ui.button("run.save", a, b, save.c_str(), PaperUI::ButtonKind::Quiet,
                        !busy && !inside.empty())) {
            startPack(app, profile.id);
        }
    }
    p.col.y = bottom;

    if (const std::string packNote = packNoteNow(app); !packNote.empty()) {
        p.col.gap(p.px(6));
        note(p, packNote);
    }

    if (!app.started) return;

    p.col.gap(p.px(10));
    p.ui.progress(ImVec2(p.col.x0, p.col.y), ImVec2(p.col.x1, p.col.y + p.px(12)),
                  app.job.progress());
    p.col.gap(p.px(12) + p.px(6));

    std::string label = app.job.currentLabel();
    if (label.empty() && app.job.finished()) {
        label = app.job.succeeded() ? "Done - your assets are ready."
                                    : "Finished, but some steps did not complete.";
    }
    const float textSize = p.px(kText);
    p.ui.text(ImVec2(p.col.x0, p.col.y), label.c_str(), textSize, p.theme().ink);
    p.col.gap(p.ui.lineHeight(textSize) + p.px(8));

    // The log, kept at its end while the build is writing to it.
    const ImVec2 la(p.col.x0, p.col.y);
    const ImVec2 lb(p.col.x1, p.col.y + p.px(kLogHeight));
    p.ui.frame(la, lb);
    const float pad = p.px(8);
    const ImVec2 ia(la.x + pad, la.y + p.px(4));
    const ImVec2 ib(lb.x - p.px(4), lb.y - p.px(4));
    const float lineSize = p.px(kSmall);
    const float lineH = p.ui.lineHeight(lineSize);
    const std::vector<std::string> lines = app.job.log();
    const float first = p.ui.beginScroll("run.log", ia, ib, app.job.running());
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const float y = first + static_cast<float>(i) * lineH;
        if (y + lineH < ia.y || y > ib.y) continue;   // out of the window onto it
        p.ui.text(ImVec2(ia.x, y), lines[i].c_str(), lineSize, p.theme().inkSoft);
    }
    p.ui.endScroll(first + static_cast<float>(lines.size()) * lineH);
    p.col.y = lb.y;
}

}  // namespace

void rescan(App& app) {
    app.gameScan = scanInstall(app.gameDir.text());
    app.secondScan = scanInstall(app.secondDir.text());
    app.cascConfirmed = false;
    app.cascError.clear();
}

void initPanelDefaults(App& app) {
    // Where the client itself looks when nobody has said otherwise. Data/
    // beside the working directory was eight minutes of extraction the client
    // would never find, with nothing on screen saying where it went.
    const fs::path userData = wowee::core::userDataRoot();
    app.outputDir.setText(userData.empty() ? (fs::current_path() / "Data").string()
                                           : userData.string());
}

bool finishedSuccessfully(const App& app) {
    return app.started && app.job.finished() && app.job.succeeded();
}

void drawPanel(App& app, PaperUI& ui, ImVec2 a, ImVec2 b) {
    // No standing header here: the standalone window and the client each say
    // why the panel is in front of somebody, and they do not say the same
    // thing. Drawn here it was said twice in the client.
    //
    // The in-window browser is a modal: everything under it is drawn but
    // hears nothing until it closes.
    const bool browsing = app.picker.isOpen();
    if (browsing) ui.pushInert();

    // The questions scroll; the button that answers them does not. With
    // everything in one scrolling page, adding a line anywhere above pushed
    // Build off the bottom of the window - the one control the whole
    // program exists to offer, reachable only by scrolling past the reading.
    const float actions = actionsHeight(app, ui, b.x - a.x);
    const ImVec2 qb(b.x, std::max(a.y + ui.px(60), b.y - actions));
    Page questions{ui, ui::Column{a.x, b.x - ui.px(kMarkRoom), 0.0f}};
    questions.col.y = ui.beginScroll("assets.questions", a, qb);
    drawGame(app, questions);
    drawBase(app, questions);
    drawUpgrades(app, questions);
    drawPlan(app, questions);
    questions.col.gap(ui.px(8));
    ui.endScroll(questions.col.y);

    ui.rule(ImVec2(a.x, qb.y + ui.px(8)), ImVec2(b.x, qb.y + ui.px(8)),
            ui::paperFade(ui.theme().pencil, 0.7f), ui.px(1.0f), 0x5EEDu);
    Page run{ui, ui::Column{a.x, b.x, qb.y + ui.px(16)}};
    drawRun(app, run);

    if (browsing) ui.popInert();

    if (std::string chosen; app.picker.draw(ui, &chosen)) {
        switch (app.pendingPick) {
            case 1: app.gameDir.setText(chosen);
                    rescan(app);
                    break;
            case 2: app.secondDir.setText(chosen);
                    rescan(app);
                    break;
            case 3: startImport(app, chosen); break;
            case 4: app.outputDir.setText(chosen); break;
            default: break;
        }
        app.pendingPick = 0;
    }
}

}  // namespace wowee::assets
