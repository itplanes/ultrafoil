#include "util/cheat_service.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <switch.h>

#include "util/config.hpp"
#include "util/curl.hpp"
#include "util/json.hpp"
#include "util/title_util.hpp"
#include "util/file_transaction.hpp"

namespace inst::cheats {
    namespace {
        constexpr std::size_t kMaxCheatFileSize = 1024U * 1024U;

        struct DmntMemoryRegionExtents {
            std::uint64_t base;
            std::uint64_t size;
        };

        // Atmosphere dmnt:cht command 65002 output ABI. This type is not
        // currently exposed by libnx, so keep the full fixed layout locally.
        struct DmntCheatProcessMetadata {
            std::uint64_t process_id;
            std::uint64_t title_id;
            DmntMemoryRegionExtents main_nso_extents;
            DmntMemoryRegionExtents heap_extents;
            DmntMemoryRegionExtents alias_extents;
            DmntMemoryRegionExtents address_space_extents;
            std::uint8_t main_nso_build_id[0x20];
        };

        static_assert(sizeof(DmntCheatProcessMetadata) == 0x70, "Unexpected dmnt metadata ABI size");

        bool IsHex16(const std::string& value)
        {
            return value.size() == 16 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
                return std::isxdigit(c) != 0;
            });
        }

        std::string CheatDirectory(const Target& target)
        {
            return "sdmc:/atmosphere/contents/" + FormatTitleId(target.titleId) + "/cheats";
        }

        std::string CheatPath(const Target& target)
        {
            return CheatDirectory(target) + "/" + target.buildId + ".txt";
        }

        std::string MetadataPath(const Target& target)
        {
            return inst::config::appDir + "/cheat_state/" + FormatTitleId(target.titleId) + "/" + target.buildId + ".json";
        }

        void ReadStringArray(const nlohmann::json& object, const char* key, std::vector<std::string>& out)
        {
            out.clear();
            if (!object.contains(key) || !object[key].is_array())
                return;
            for (const auto& value : object[key]) {
                if (value.is_string())
                    out.push_back(value.get<std::string>());
            }
        }

        std::string ReadFile(const std::string& path)
        {
            if (std::filesystem::file_size(path) > kMaxCheatFileSize)
                throw std::runtime_error("File exceeds the safe read limit.");
            std::ifstream input(path, std::ios::binary);
            if (!input) throw std::runtime_error("Unable to read " + path);
            return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        }

        std::string ContentHash(const std::string& content)
        {
            std::uint8_t hash[32]{};
            sha256CalculateHash(hash, content.data(), content.size());
            std::ostringstream result;
            for (auto value : hash) result << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(value);
            return result.str();
        }

        nlohmann::json ReadState(const Target& target)
        {
            if (!std::filesystem::exists(MetadataPath(target))) return nlohmann::json::object();
            try { return nlohmann::json::parse(ReadFile(MetadataPath(target))); }
            catch (...) { return nlohmann::json::object(); }
        }

        void RecordAction(const Target& target, const std::string& action)
        {
            try {
                const auto path = inst::config::appDir + "/cheat_history.jsonl";
                if (std::filesystem::exists(path) && std::filesystem::file_size(path) > 1024U * 1024U) {
                    std::filesystem::remove(path + ".old");
                    std::filesystem::rename(path, path + ".old");
                }
                std::ofstream output(path, std::ios::app);
                output << nlohmann::json({{"title_id", FormatTitleId(target.titleId)}, {"build_id", target.buildId},
                    {"action", action}, {"timestamp", std::time(nullptr)}}).dump() << '\n';
            } catch (...) { /* The installation result does not depend on the history log. */ }
        }

        std::string ApiBase()
        {
            std::string base = inst::config::remoteUrl;
            while (!base.empty() && base.back() == '/')
                base.pop_back();
            return base;
        }
    }

    std::string FormatTitleId(std::uint64_t titleId)
    {
        std::ostringstream out;
        out << std::uppercase << std::hex << std::setw(16) << std::setfill('0') << titleId;
        return out.str();
    }

    bool GetRunningTarget(Target& out, std::string& error)
    {
        // The dmnt metadata flow follows the GPL-3.0 AIO-Switch-Updater
        // implementation, with explicit Result validation added here.
        out = {};
        error.clear();
        Service dmnt{};
        Result rc = smGetService(&dmnt, "dmnt:cht");
        if (R_FAILED(rc)) {
            error = "Atmosphere cheat service is unavailable. Start a game and try again.";
            return false;
        }

        DmntCheatProcessMetadata metadata{};
        rc = serviceDispatch(&dmnt, 65003);
        if (R_SUCCEEDED(rc))
            rc = serviceDispatchOut(&dmnt, 65002, metadata);
        serviceClose(&dmnt);
        if (R_FAILED(rc) || metadata.title_id == 0) {
            error = "No running game was detected. Leave a game running in the background and reopen Cheats.";
            return false;
        }

        out.titleId = metadata.title_id;
        std::ostringstream bid;
        bid << std::uppercase << std::hex << std::setfill('0');
        for (std::size_t i = 0; i < 8; i++)
            bid << std::setw(2) << static_cast<unsigned int>(metadata.main_nso_build_id[i]);
        out.buildId = bid.str();
        if (!IsHex16(out.buildId)) {
            error = "The running game returned an invalid Build ID.";
            out = {};
            return false;
        }
        return true;
    }

    bool FetchExact(const Target& target, std::vector<Entry>& out, std::vector<std::string>& availableBuildIds, std::string& error, const std::atomic_bool* cancel)
    {
        out.clear();
        availableBuildIds.clear();
        error.clear();
        if (target.titleId == 0 || !IsHex16(target.buildId)) {
            error = "Invalid Title ID or Build ID.";
            return false;
        }
        const std::string base = ApiBase();
        if (base.empty()) {
            error = "Configure an AeroFoil Remote before searching for cheats.";
            return false;
        }

        const std::string url = base + "/api/cheats/titles/" + FormatTitleId(target.titleId) + "/builds/" + target.buildId;
        const std::string body = inst::curl::downloadToBufferWithAuth(url, inst::config::remoteUser, inst::config::remotePass, 20000L, cancel, &error);
        if (body.empty()) {
            if (error.empty()) error = "AeroFoil did not return cheat data.";
            return false;
        }

        try {
            const auto json = nlohmann::json::parse(body);
            if (json.contains("provider_errors") && json["provider_errors"].is_array() &&
                json["provider_errors"].size() >= 3 &&
                (!json.contains("available_build_ids") || json["available_build_ids"].empty())) {
                error = "AeroFoil could not reach any configured cheat provider.";
                return false;
            }
            if (json.contains("available_build_ids") && json["available_build_ids"].is_array()) {
                for (const auto& value : json["available_build_ids"]) {
                    if (value.is_string() && IsHex16(value.get<std::string>()))
                        availableBuildIds.push_back(value.get<std::string>());
                }
            }
            if (!json.contains("cheats") || !json["cheats"].is_array())
                return true;
            for (const auto& item : json["cheats"]) {
                if (!item.is_object())
                    continue;
                Entry entry;
                entry.id = item.value("id", "");
                entry.name = item.value("name", "Unnamed cheat");
                entry.content = item.value("content", "");
                entry.source = item.value("source", "unknown");
                entry.role = item.value("role", "normal");
                ReadStringArray(item, "tags", entry.tags);
                ReadStringArray(item, "conflict_groups", entry.conflictGroups);
                if (entry.id.size() != 64 || entry.content.empty() || entry.content.size() > kMaxCheatFileSize)
                    continue;
                out.push_back(std::move(entry));
            }
            return true;
        } catch (const std::exception& ex) {
            error = std::string("Invalid cheat response: ") + ex.what();
            return false;
        }
    }

    bool FetchBuildIndex(const InstalledTitle& title, const std::string& actualBuildId, std::vector<BuildBundle>& out, std::string& error, const std::atomic_bool* cancel)
    {
        out.clear();
        if (ApiBase().empty()) { error = "Configure an AeroFoil Remote in Settings."; return false; }
        std::string url = ApiBase() + "/api/cheats/titles/" + FormatTitleId(title.titleId) + "/index?installed_version=" + std::to_string(title.version);
        if (IsHex16(actualBuildId)) url += "&actual_build_id=" + actualBuildId;
        const auto body = inst::curl::downloadToBufferWithAuth(url, inst::config::remoteUser, inst::config::remotePass, 30000L, cancel, &error);
        if (body.empty()) return false;
        try {
            const auto json = nlohmann::json::parse(body);
            for (const auto& item : json.at("builds")) {
                BuildBundle build;
                build.buildId = item.value("build_id", "");
                if (!IsHex16(build.buildId)) continue;
                build.entryCount = item.value("cheat_count", 0U);
                build.versionKnown = item.contains("version") && item["version"].is_number_integer() && item["version"].get<std::int64_t>() >= 0;
                if (build.versionKnown) build.version = item["version"].get<std::uint32_t>();
                build.versionLabel = item.value("version_label", "");
                build.compatibility = item.value("compatibility", "unknown");
                ReadStringArray(json, "attributions", build.attributions);
                out.push_back(std::move(build));
            }
            std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
                const auto rank = [](const auto& state) { return state == "exact" ? 0 : state == "version_recommended" ? 1 : 2; };
                return rank(a.compatibility) < rank(b.compatibility);
            });
            if (out.empty() && json.contains("provider_errors") && !json["provider_errors"].empty()) {
                error = "No local codes; providers failed. Check AeroFoil database status.";
                return false;
            }
            return true;
        } catch (const std::exception& ex) { error = std::string("Invalid cheat index: ") + ex.what(); return false; }
    }

    std::string Diagnostics(const std::atomic_bool* cancel)
    {
        std::string error;
        const auto body = inst::curl::downloadToBufferWithAuth(ApiBase() + "/api/client/status", inst::config::remoteUser, inst::config::remotePass, 10000L, cancel, &error);
        std::string report = "UltraFoil " + inst::config::appVersion + "\n";
        try {
            const auto status = nlohmann::json::parse(body);
            report += "AeroFoil " + status.value("version", "unknown") + " / API " + std::to_string(status.value("api_version", 0)) + "\n";
            report += status.value("api_version", 0) >= 2 ? "Cheat index: supported\n" : "Cheat index: update server\n";
            if (status.contains("cheat_database")) {
                const auto& database = status["cheat_database"];
                if (database.contains("source_commit") && database["source_commit"].is_string()) report += "Cheat DB: " + database["source_commit"].get<std::string>().substr(0, 12) + "\n";
                if (database.contains("last_error") && database["last_error"].is_string()) report += "Sync error: " + database["last_error"].get<std::string>() + "\n";
            }
        } catch (...) { report += "Server: " + (error.empty() ? "invalid response" : error) + "\n"; }
        Target target;
        report += GetRunningTarget(target, error) ? "dmnt: ready / BID " + target.buildId : "dmnt: " + error;
        std::error_code ec;
        const auto space = std::filesystem::space(inst::config::appDir, ec);
        if (!ec) report += "\nSD free: " + std::to_string(space.available / (1024 * 1024)) + " MiB";
        return report;
    }

    bool ListInstalledTitles(std::vector<InstalledTitle>& out, std::string& error)
    {
        out.clear();
        error.clear();
        Result rc = nsInitialize();
        if (R_FAILED(rc)) {
            std::ostringstream message;
            message << "Unable to initialize the application service (0x" << std::uppercase << std::hex << rc << ").";
            error = message.str();
            return false;
        }
        constexpr s32 chunk = 64;
        s32 offset = 0;
        while (true) {
            NsApplicationRecord records[chunk]{};
            s32 count = 0;
            rc = nsListApplicationRecord(records, chunk, offset, &count);
            if (R_FAILED(rc)) {
                std::ostringstream message;
                message << "Unable to list installed games (0x" << std::uppercase << std::hex << rc << ").";
                error = message.str();
                nsExit();
                return false;
            }
            if (count <= 0)
                break;
            for (s32 i = 0; i < count; i++) {
                if (records[i].application_id == 0)
                    continue;
                s32 metaCount = 0;
                if (R_FAILED(nsCountApplicationContentMeta(records[i].application_id, &metaCount)) || metaCount <= 0)
                    continue;
                InstalledTitle title;
                title.titleId = records[i].application_id;
                std::vector<NsApplicationContentMetaStatus> statuses(static_cast<std::size_t>(metaCount));
                s32 statusCount = 0;
                if (R_SUCCEEDED(nsListApplicationContentMetaStatus(title.titleId, 0, statuses.data(), metaCount, &statusCount))) {
                    for (s32 j = 0; j < statusCount; j++) {
                        if (statuses[j].meta_type == NcmContentMetaType_Patch)
                            title.version = std::max(title.version, statuses[j].version);
                        else if (statuses[j].meta_type == NcmContentMetaType_Application && title.version == 0)
                            title.version = statuses[j].version;
                    }
                }
                title.name = tin::util::GetBaseTitleName(title.titleId);
                if (title.name.empty())
                    title.name = FormatTitleId(title.titleId);
                out.push_back(std::move(title));
            }
            offset += count;
            if (count < chunk)
                break;
        }
        nsExit();
        std::sort(out.begin(), out.end(), [](const InstalledTitle& a, const InstalledTitle& b) { return a.name < b.name; });
        return true;
    }

    bool FetchAllBuilds(std::uint64_t titleId, std::vector<BuildBundle>& out, std::string& error)
    {
        out.clear();
        error.clear();
        if (titleId == 0 || ApiBase().empty()) {
            error = "Configure an AeroFoil Remote before downloading cheats.";
            return false;
        }
        const std::string url = ApiBase() + "/api/cheats/titles/" + FormatTitleId(titleId) + "/bundle";
        const std::string body = inst::curl::downloadToBufferWithAuth(url, inst::config::remoteUser, inst::config::remotePass, 60000L);
        if (body.empty()) {
            error = "AeroFoil did not return a cheat bundle.";
            return false;
        }
        try {
            const auto json = nlohmann::json::parse(body);
            if (!json.contains("builds") || !json["builds"].is_array())
                return true;
            for (const auto& item : json["builds"]) {
                BuildBundle build;
                build.buildId = item.value("build_id", "");
                build.content = item.value("content", "");
                build.entryCount = item.value("entry_count", 0U);
                build.version = item.value("version", 0U);
                build.versionLabel = item.value("version_label", "");
                if (json.contains("attributions") && json["attributions"].is_array()) {
                    for (const auto& value : json["attributions"])
                        if (value.is_string()) build.attributions.push_back(value.get<std::string>());
                }
                if (!IsHex16(build.buildId) || build.content.empty() || build.content.size() > kMaxCheatFileSize)
                    continue;
                if (item.contains("conflicts") && item["conflicts"].is_array()) {
                    for (const auto& conflict : item["conflicts"]) {
                        const std::string group = conflict.value("group", "");
                        if (!group.empty()) build.conflictGroups.push_back(group);
                    }
                }
                out.push_back(std::move(build));
            }
            return true;
        } catch (const std::exception& ex) {
            error = std::string("Invalid cheat bundle: ") + ex.what();
            return false;
        }
    }

    bool InstallAllBuilds(std::uint64_t titleId, const std::vector<BuildBundle>& builds, std::size_t& installed, std::string& error)
    {
        installed = 0;
        for (const auto& build : builds) {
            Target target{titleId, build.buildId};
            Entry entry;
            entry.id = "bundle";
            entry.name = "AeroFoil managed bundle";
            entry.source = "AeroFoil";
            entry.content = build.content;
            entry.conflictGroups = build.conflictGroups;
            if (!Install(target, entry, error))
                return false;
            installed++;
        }
        return true;
    }

    bool RemoveAllBuilds(std::uint64_t titleId, const std::vector<BuildBundle>& builds, std::size_t& removed, std::string& error)
    {
        removed = 0;
        for (const auto& build : builds) {
            Target target{titleId, build.buildId};
            if (IsInstalled(target)) removed++;
            if (!Remove(target, error)) return false;
        }
        return true;
    }

    bool Install(const Target& target, const Entry& entry, std::string& error)
    {
        error.clear();
        if (target.titleId == 0 || !IsHex16(target.buildId) || entry.content.empty() || entry.content.size() > kMaxCheatFileSize) {
            error = "Invalid cheat file.";
            return false;
        }
        try {
            const std::string targetPath = CheatPath(target);
            inst::files::RecoverPair(targetPath, MetadataPath(target));
            auto state = ReadState(target);
            std::string originalBackup;
            if (std::filesystem::exists(targetPath)) {
                const std::string backupDir = inst::config::appDir + "/cheat_backups/" + FormatTitleId(target.titleId);
                std::filesystem::create_directories(backupDir);
                const std::string backupPath = backupDir + "/" + target.buildId + "." + std::to_string(armGetSystemTick()) + ".txt";
                std::filesystem::copy_file(targetPath, backupPath);
                const bool managed = state.value("content_sha256", "") == ContentHash(ReadFile(targetPath));
                originalBackup = managed ? state.value("original_backup", "") : backupPath;
            }
            std::string content = entry.content;
            if (content.back() != '\n') content += '\n';
            const auto metadata = nlohmann::json({{"id", entry.id}, {"name", entry.name}, {"source", entry.source},
                {"tags", entry.tags}, {"conflict_groups", entry.conflictGroups}, {"content_sha256", ContentHash(content)},
                {"original_backup", originalBackup}, {"role", entry.role}, {"selected_ids", entry.selectedIds}}).dump(2);
            if (!inst::files::CommitPair(targetPath, MetadataPath(target), content, metadata, error)) return false;
            RecordAction(target, "installed");
            return true;
        } catch (const std::exception& ex) {
            error = ex.what();
            return false;
        }
    }

    bool Remove(const Target& target, std::string& error)
    {
        error.clear();
        if (target.titleId == 0 || !IsHex16(target.buildId)) { error = "Invalid cheat target."; return false; }
        try {
            const std::string path = CheatPath(target);
            inst::files::RecoverPair(path, MetadataPath(target));
            if (!std::filesystem::exists(path)) {
                const std::string metadataPath = MetadataPath(target);
                if (std::filesystem::exists(metadataPath))
                    std::filesystem::remove(metadataPath);
                return true;
            }
            const auto state = ReadState(target);
            if (state.value("content_sha256", "") != ContentHash(ReadFile(path))) {
                error = "This file is unmanaged or has been edited outside UltraFoil. It was kept unchanged.";
                return false;
            }
            std::optional<std::string> original;
            const auto originalPath = state.value("original_backup", "");
            if (!originalPath.empty()) {
                const auto prefix = inst::config::appDir + "/cheat_backups/" + FormatTitleId(target.titleId) + "/";
                if (originalPath.rfind(prefix, 0) != 0 || originalPath.find("..") != std::string::npos)
                    throw std::runtime_error("Invalid original backup path.");
                original = ReadFile(originalPath);
            }
            if (!inst::files::CommitPair(path, MetadataPath(target), original, std::nullopt, error)) return false;
            RecordAction(target, original ? "removed_and_restored_original" : "removed");
            return true;
        } catch (const std::exception& ex) {
            error = ex.what();
            return false;
        }
    }

    bool IsInstalled(const Target& target)
    {
        if (target.titleId == 0 || !IsHex16(target.buildId)) return false;
        try { inst::files::RecoverPair(CheatPath(target), MetadataPath(target)); return std::filesystem::exists(CheatPath(target)); }
        catch (...) { return false; }
    }

    bool GetInstalledMetadata(const Target& target, Entry& out)
    {
        out = {};
        try {
            inst::files::RecoverPair(CheatPath(target), MetadataPath(target));
            std::ifstream input(MetadataPath(target), std::ios::binary);
            if (!input)
                return false;
            nlohmann::json json;
            input >> json;
            if (!json.is_object())
                return false;
            if (!std::filesystem::exists(CheatPath(target)) || json.value("content_sha256", "") != ContentHash(ReadFile(CheatPath(target))))
                return false;
            out.id = json.value("id", "");
            out.name = json.value("name", "Installed cheat");
            out.source = json.value("source", "unknown");
            ReadStringArray(json, "tags", out.tags);
            ReadStringArray(json, "conflict_groups", out.conflictGroups);
            ReadStringArray(json, "selected_ids", out.selectedIds);
            return !out.id.empty();
        } catch (...) {
            return false;
        }
    }

    std::vector<std::string> FindConflicts(const Entry& installed, const Entry& candidate)
    {
        std::vector<std::string> conflicts;
        for (const auto& candidateGroup : candidate.conflictGroups) {
            if (std::find(installed.conflictGroups.begin(), installed.conflictGroups.end(), candidateGroup) != installed.conflictGroups.end() &&
                std::find(conflicts.begin(), conflicts.end(), candidateGroup) == conflicts.end()) {
                conflicts.push_back(candidateGroup);
            }
        }
        return conflicts;
    }

    std::string FormatTags(const Entry& entry)
    {
        std::string result;
        auto add = [&](const char* value) {
            if (!result.empty())
                result += " ";
            result += value;
        };
        for (const auto& tag : entry.tags) {
            if (tag == "fps")
                add("[FPS]");
            else if (tag == "resolution")
                add("[RES]");
            else if (tag == "graphics")
                add("[GFX]");
        }
        if (result.empty())
            add("[CHEAT]");
        return result;
    }
}
