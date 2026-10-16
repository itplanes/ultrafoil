#include "ui/cheatsPage.hpp"
#include "ui/MainApplication.hpp"
#include "util/config.hpp"
#include "util/file_transaction.hpp"
#include "util/json.hpp"
#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>

namespace {
    // Fit by UTF-8 codepoint, counting wide text twice. No byte-sliced glyphs.
    std::string Fit(const std::string& text, std::size_t cells = 48) {
        std::size_t end = 0, used = 0;
        while (end < text.size()) {
            const auto c = static_cast<unsigned char>(text[end]);
            const std::size_t length = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
            const std::size_t width = c < 0x80 ? 1 : 2;
            if (used + width > cells || end + length > text.size()) break;
            end += length; used += width;
        }
        return text.substr(0, end) + (end < text.size() ? "..." : "");
    }
    std::string Match(const std::string& state) {
        if (state == "exact") return "Actual BID verified";
        if (state == "version_recommended") return "Version recommended / BID unverified";
        if (state == "incompatible") return "Incompatible with device";
        return "Insufficient matching information";
    }
    std::string BoundedLines(const std::string& text, int skip = 0) {
        std::string wrapped; std::size_t cells = 0;
        for (std::size_t index = 0; index < text.size();) {
            const auto c = static_cast<unsigned char>(text[index]);
            const std::size_t length = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
            if (index + length > text.size()) break;
            const std::size_t width = c < 0x80 ? 1 : 2;
            if (text[index] == '\n') cells = 0;
            else if (cells + width > 58) { wrapped += '\n'; cells = width; }
            else cells += width;
            wrapped.append(text, index, length); index += length;
        }
        const int lines = static_cast<int>(std::count(wrapped.begin(), wrapped.end(), '\n')) + 1;
        skip = std::min(skip, std::max(0, lines - 17));
        std::istringstream input(wrapped); std::string line, out; int count = 0;
        while (skip-- > 0 && std::getline(input, line)) {}
        while (std::getline(input, line) && count++ < 17) out += Fit(line, 58) + "\n";
        return out;
    }
}

namespace inst::ui {
    CheatsPage::CheatsPage() {
        const auto color = [](const char* value) { return pu::ui::Color::FromHex(value); };
        SetBackgroundColor(color(inst::config::oledMode ? "#000000FF" : "#170909FF"));
        Add(pu::ui::elm::Rectangle::New(576, 100, 2, 535, color("#FFFFFF33")));
        heading = pu::ui::elm::TextBlock::New(28, 26, "Cheat manager", 32);
        status = pu::ui::elm::TextBlock::New(28, 90, "", 20);
        detail = pu::ui::elm::TextBlock::New(600, 145, "", 20);
        hints = pu::ui::elm::TextBlock::New(28, 672, "", 18);
        for (const auto& text : {heading, status, detail, hints}) { text->SetColor(color("#FFFFFFFF")); Add(text); }
        menu = pu::ui::elm::Menu::New(28, 145, 524, color("#FFFFFF00"), 50, 10, 22);
        menu->SetOnFocusColor(color("#FFFFFF33"));
        menu->SetScrollbarColor(color("#FF4D4DFF"));
        menu->SetOnSelectionChanged([this]() { detailOffset = 0; ShowDetail(); });
        Add(menu);
        SetOnInput([this](u64 down, u64 up, u64 held, pu::ui::Touch touch) { OnInput(down, up, held, touch); });
    }
    CheatsPage::~CheatsPage() { Stop(); }
    void CheatsPage::Stop() { cancelled = true; if (worker.joinable()) worker.join(); }
    void CheatsPage::Open() { mainApp->LoadLayout(mainApp->cheatsPage); if (!busy) Scan(); }
    void CheatsPage::Run(std::function<std::function<void()>()> job, bool canCancel) {
        if (busy) return;
        if (worker.joinable()) worker.join();
        cancelled = false; busy = true; cancellable = canCancel; menu->SetVisible(false);
        status->SetText(canCancel ? "Working... Press B to cancel" : "Committing files... Please wait");
        worker = std::thread([this, job = std::move(job)]() {
            std::function<void()> completed;
            try { completed = job(); }
            catch (const std::exception& ex) { const std::string message = ex.what(); completed = [this, message]() { status->SetText(Fit(message, 100)); }; }
            catch (...) { completed = [this]() { status->SetText("Operation failed."); }; }
            std::lock_guard<std::mutex> guard(resultMutex); result = std::move(completed);
        });
    }
    void CheatsPage::Poll() {
        std::function<void()> completed;
        { std::lock_guard<std::mutex> guard(resultMutex); completed.swap(result); }
        if (!completed) return;
        if (worker.joinable()) worker.join();
        busy = false; menu->SetVisible(true);
        if (cancelled) { status->SetText("Request cancelled. Press A to retry or B to return."); return; }
        completed();
    }
    void CheatsPage::Scan() {
        Run([this]() {
            std::vector<inst::cheats::InstalledTitle> found; inst::cheats::Target active; std::string error, runningError;
            inst::cheats::GetRunningTarget(active, runningError);
            const bool ok = inst::cheats::ListInstalledTitles(found, error);
            if (active.titleId && std::none_of(found.begin(), found.end(), [&](const auto& t) { return t.titleId == active.titleId; }))
                found.insert(found.begin(), {active.titleId, "Running game", 0});
            return [this, found = std::move(found), active, error, ok]() mutable {
                games = std::move(found); running = active; view = View::Games; gameIndex = 0; Rebuild();
                if (!ok && games.empty()) status->SetText(Fit(error, 100));
            };
        });
    }
    void CheatsPage::LoadBuilds() {
        if (gameIndex >= games.size()) return;
        const auto game = games[gameIndex]; const auto actual = running.titleId == game.titleId ? running.buildId : "";
        Run([this, game, actual]() {
            std::vector<inst::cheats::BuildBundle> found; std::string error;
            const bool ok = inst::cheats::FetchBuildIndex(game, actual, found, error, &cancelled);
            return [this, found = std::move(found), error, ok]() mutable {
                if (!ok) { status->SetText(Fit(error, 100)); return; }
                builds = std::move(found); buildIndex = 0; view = View::Builds; Rebuild();
            };
        });
    }
    void CheatsPage::LoadEntries() {
        if (buildIndex >= builds.size()) return;
        const inst::cheats::Target target{games[gameIndex].titleId, builds[buildIndex].buildId};
        Run([this, target]() {
            std::vector<inst::cheats::Entry> found; std::vector<std::string> available; std::string error;
            const bool ok = inst::cheats::FetchExact(target, found, available, error, &cancelled);
            inst::cheats::Entry installed;
            inst::cheats::GetInstalledMetadata(target, installed);
            return [this, found = std::move(found), installed, error, ok]() mutable {
                if (!ok) { status->SetText(Fit(error, 100)); return; }
                entries = std::move(found); selected.assign(entries.size(), false);
                const auto masters = std::count_if(entries.begin(), entries.end(), [](const auto& entry) { return entry.role == "master"; });
                for (std::size_t i = 0; i < entries.size(); ++i) selected[i] = (masters == 1 && entries[i].role == "master") ||
                    std::find(installed.selectedIds.begin(), installed.selectedIds.end(), entries[i].id) != installed.selectedIds.end();
                view = View::Entries; Rebuild();
            };
        });
    }
    void CheatsPage::Rebuild() {
        menu->ClearItems();
        auto add = [&](const std::string& label) { auto item = pu::ui::elm::MenuItem::New(Fit(label, 39)); item->SetColor(pu::ui::Color::FromHex("#FFFFFFFF")); menu->AddItem(item); };
        if (view == View::Games) { for (const auto& game : games) add(game.name); status->SetText(std::to_string(games.size()) + " installed games. Select one to check cheats."); }
        if (view == View::Builds) { for (const auto& build : builds) add((build.compatibility == "exact" ? "[BID] " : build.compatibility == "version_recommended" ? "[Version] " : "[?] ") + build.buildId); status->SetText(Fit(games[gameIndex].name, 90)); }
        if (view == View::Entries) { for (std::size_t i = 0; i < entries.size(); ++i) add(std::string(selected[i] ? "[x] " : "[ ] ") + entries[i].name); status->SetText(Match(builds[buildIndex].compatibility)); }
        hints->SetText("A: select   X: install / refresh   Y: remove   L/R: details   +: checks   -: history   B: back");
        ShowDetail();
    }
    void CheatsPage::ShowDetail() {
        const auto index = static_cast<std::size_t>(menu->GetSelectedIndex()); std::string text;
        if (view == View::Games && index < games.size()) {
            const auto& game = games[index];
            text = game.name + "\nTitle ID: " + inst::cheats::FormatTitleId(game.titleId) + "\nInstalled internal version: " + std::to_string(game.version) + "\n\n";
            text += running.titleId == game.titleId ? "Running BID: " + running.buildId : "Game is not running.\nVersion recommendations do not verify BID.";
            text += "\n\nA: list available builds\nX: rescan installed games\nDownloaded / installed / enabled\nare different states.";
        } else if (view == View::Builds && index < builds.size()) {
            const auto& build = builds[index];
            text = "BID: " + build.buildId + "\n" + Match(build.compatibility) + "\nInternal version: " + (build.versionKnown ? "v" + std::to_string(build.version) : "unknown") + "\nGame display version: not provided\nCodes: " + std::to_string(build.entryCount);
            if (!build.attributions.empty()) text += "\nAuthor/source:";
            for (const auto& author : build.attributions) text += "\n" + author;
            for (const auto& warning : build.providerErrors) text += "\nProvider warning: " + warning;
            if (build.versionKnown && build.version > games[gameIndex].version) text += "\nA newer version has codes.\nDo not install it for this version.";
            text += "\n\nA: download only this build's codes\nNo code files are installed yet.";
        } else if (view == View::Entries && index < entries.size()) {
            const auto& entry = entries[index]; const inst::cheats::Target target{games[gameIndex].titleId, builds[buildIndex].buildId};
            inst::cheats::Entry managed;
            text = entry.name + "\n" + inst::cheats::FormatTags(entry) + " / " + entry.role + "\nSource: " + entry.source + "\nBID: " + target.buildId + "\n" + Match(builds[buildIndex].compatibility) + "\n";
            text += inst::cheats::GetInstalledMetadata(target, managed) ? "Installed: UltraFoil managed" : inst::cheats::IsInstalled(target) ? "Installed: external / legacy / edited" : "Installed: no";
            text += "\nEnabled: unknown (check overlay)\n\n" + entry.content;
        }
        if (view == View::Diagnostics || view == View::History) text = extraDetail;
        detail->SetText(BoundedLines(text.empty() ? "No results. Press B to return." : text, detailOffset));
    }
    void CheatsPage::InstallSelection() {
        if (view != View::Entries || entries.empty()) return;
        const auto& build = builds[buildIndex];
        if (build.compatibility != "exact" && build.compatibility != "version_recommended") {
            mainApp->CreateShowDialog("Cannot confirm compatibility", "Start the game to verify the actual BID. No file was changed.", {"OK"}, true); return;
        }
        inst::cheats::Entry candidate; candidate.id = "selection"; candidate.name = "UltraFoil selected codes"; candidate.source = "AeroFoil";
        std::map<std::string, std::vector<std::string>> groups; std::size_t count = 0;
        for (std::size_t i = 0; i < entries.size(); ++i) if (selected[i]) {
            candidate.content += entries[i].content + "\n\n"; ++count;
            candidate.selectedIds.push_back(entries[i].id);
            if (entries[i].role != "master") for (const auto& group : entries[i].conflictGroups) groups[group].push_back(entries[i].name);
        }
        if (!count) { status->SetText("Select at least one code with A."); return; }
        const auto masters = std::count_if(entries.begin(), entries.end(), [](const auto& entry) { return entry.role == "master"; });
        std::size_t selectedMasters = 0;
        for (std::size_t i = 0; i < entries.size(); ++i) if (selected[i] && entries[i].role == "master") ++selectedMasters;
        if (selectedMasters > 1) {
            mainApp->CreateShowDialog("Multiple master codes", "Choose one master variant before installing.\nThe dependency between these codes is not known.", {"OK"}, true); return;
        }
        if (masters && std::none_of(entries.begin(), entries.end(), [&](const auto& entry) {
            const auto index = static_cast<std::size_t>(&entry - entries.data()); return entry.role == "master" && selected[index];
        })) { status->SetText("Select the required master code first."); return; }
        std::string prompt = Match(build.compatibility) + "\nBID: " + build.buildId + "\nSelected: " + std::to_string(count) + "\n";
        for (const auto& group : groups) if (group.second.size() > 1) {
            mainApp->CreateShowDialog("Conflicting selections", Fit(group.second[0], 45) + "\nand\n" + Fit(group.second[1], 45) + "\nChoose one for " + group.first + ".", {"OK"}, true); return;
        }
        for (const auto& group : groups) candidate.conflictGroups.push_back(group.first);
        if (candidate.content.size() > 1024U * 1024U) { status->SetText("Selection too large. Select fewer codes."); return; }
        prompt += build.compatibility == "version_recommended" ? "\nBID is not verified on this device.\n" : "\n";
        prompt += "Existing files are backed up.\nOverlay controls enable/disable.\nKeep Atmosphere default cheats OFF.";
        if (mainApp->CreateShowDialog("Install selected codes?", prompt, {"Install", "Cancel"}, false) != 0) return;
        const inst::cheats::Target target{games[gameIndex].titleId, build.buildId};
        const auto expected = games[gameIndex]; const bool exact = build.compatibility == "exact";
        Run([this, target, candidate, expected, exact]() {
            std::string error; bool valid = false;
            if (exact) { inst::cheats::Target actual; valid = inst::cheats::GetRunningTarget(actual, error) && actual.titleId == target.titleId && actual.buildId == target.buildId; }
            else { std::vector<inst::cheats::InstalledTitle> current; if (inst::cheats::ListInstalledTitles(current, error)) valid = std::any_of(current.begin(), current.end(), [&](const auto& title) { return title.titleId == expected.titleId && title.version == expected.version; }); }
            const bool ok = valid && inst::cheats::Install(target, candidate, error);
            if (!valid) error = "Game identity/version changed. Rescan before installing.";
            return [this, ok, error]() { status->SetText(ok ? "Installed. Restart / reload overlay. Check the actual activation there." : Fit(error, 100)); ShowDetail(); };
        }, false);
    }
    void CheatsPage::OnInput(u64 down, u64, u64, pu::ui::Touch) {
        if (busy) { if (cancellable && (down & HidNpadButton_B)) { cancelled = true; status->SetText("Cancelling request..."); } return; }
        const auto index = static_cast<std::size_t>(menu->GetSelectedIndex());
        if (down & HidNpadButton_B) {
            if (view == View::Games) mainApp->LoadLayout(mainApp->mainPage);
            else { view = view == View::Entries ? View::Builds : View::Games; Rebuild(); }
        } else if (down & (HidNpadButton_L | HidNpadButton_R)) {
            detailOffset = (down & HidNpadButton_L) ? std::max(0, detailOffset - 12) : detailOffset + 12; ShowDetail();
        } else if (down & HidNpadButton_A) {
            if (view == View::Games && index < games.size()) { gameIndex = index; LoadBuilds(); }
            else if (view == View::Builds && index < builds.size()) { buildIndex = index; LoadEntries(); }
            else if (view == View::Entries && index < selected.size()) { const auto masters=std::count_if(entries.begin(),entries.end(),[](const auto& entry){return entry.role=="master";});if (entries[index].role != "master" || masters > 1) selected[index] = !selected[index]; const auto keep = menu->GetSelectedIndex(); Rebuild(); menu->SetSelectedIndex(keep); }
        } else if (down & HidNpadButton_X) { if (view == View::Games) Scan(); else InstallSelection(); }
        else if ((down & HidNpadButton_Y) && view == View::Entries) {
            const inst::cheats::Target target{games[gameIndex].titleId, builds[buildIndex].buildId};
            if (mainApp->CreateShowDialog("Remove managed selection?", "External or edited files are protected.\nAn original replaced file is restored.", {"Remove", "Cancel"}, false) == 0)
                Run([this, target]() { std::string error; bool ok = inst::cheats::Remove(target, error); return [this, ok, error]() { status->SetText(ok ? "Managed selection removed; original restored if present." : Fit(error, 100)); ShowDetail(); }; }, false);
        } else if (down & HidNpadButton_Plus) {
            Run([this]() { const auto report = inst::cheats::Diagnostics(&cancelled); return [this, report]() { view = View::Diagnostics; extraDetail = report; detailOffset=0; menu->ClearItems(); ShowDetail(); status->SetText("Client / server / dmnt / SD diagnostics"); }; });
        } else if (down & HidNpadButton_Minus) {
            std::ifstream input(inst::config::appDir + "/cheat_history.jsonl"); std::string line; std::vector<std::string> recent;
            while (std::getline(input, line)) { try { const auto item = nlohmann::json::parse(line); recent.push_back(item.value("action", "") + " " + item.value("build_id", "")); if (recent.size() > 4) recent.erase(recent.begin()); } catch (...) {} }
            std::reverse(recent.begin(), recent.end()); std::string report; for (const auto& item : recent) report += item + "\n";
            report = "Cheat operations\n" + (report.empty() ? "No records yet.\n" : report) + "\nRemote / LAN installs\n";
            std::ifstream installs(inst::config::appDir + "/logs/install-history.jsonl"); recent.clear();
            while(std::getline(installs,line)){try{const auto item=nlohmann::json::parse(line);recent.push_back(item.value("at","")+" "+item.value("outcome","")+"\n"+item.value("item",""));if(recent.size()>4)recent.erase(recent.begin());}catch(...){}}
            std::reverse(recent.begin(),recent.end());for(const auto& item:recent)report+=item+"\n";
            if(recent.empty())report+="No records yet.\n";
            view = View::History; extraDetail=report;detailOffset=0;menu->ClearItems(); ShowDetail(); status->SetText("Recent operations (newest first)");
        }
    }
}
