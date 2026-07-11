#include "util/cheat_service.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <switch.h>

#include "util/config.hpp"
#include "util/curl.hpp"
#include "util/json.hpp"
#include "util/title_util.hpp"

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

        bool WriteMetadata(const Target& target, const Entry& entry)
        {
            const std::string path = MetadataPath(target);
            const std::string tempPath = path + ".tmp";
            std::filesystem::create_directories(std::filesystem::path(path).parent_path());
            nlohmann::json json = {
                {"id", entry.id},
                {"name", entry.name},
                {"source", entry.source},
                {"tags", entry.tags},
                {"conflict_groups", entry.conflictGroups}
            };
            std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
            if (!output)
                return false;
            output << json.dump(2) << '\n';
            output.close();
            if (!output)
                return false;
            if (std::filesystem::exists(path))
                std::filesystem::remove(path);
            std::filesystem::rename(tempPath, path);
            return true;
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

    bool FetchExact(const Target& target, std::vector<Entry>& out, std::vector<std::string>& availableBuildIds, std::string& error)
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
        const std::string body = inst::curl::downloadToBufferWithAuth(url, inst::config::remoteUser, inst::config::remotePass, 20000L);
        if (body.empty()) {
            error = "AeroFoil did not return cheat data.";
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
            const std::string directory = CheatDirectory(target);
            const std::string targetPath = CheatPath(target);
            const std::string tempPath = targetPath + ".tmp";
            std::filesystem::create_directories(directory);
            std::ofstream temp(tempPath, std::ios::binary | std::ios::trunc);
            if (!temp) {
                error = "Unable to create the temporary cheat file.";
                return false;
            }
            temp << entry.content;
            if (entry.content.back() != '\n')
                temp << '\n';
            temp.flush();
            temp.close();
            if (!temp) {
                std::filesystem::remove(tempPath);
                error = "Unable to finish writing the cheat file.";
                return false;
            }

            if (std::filesystem::exists(targetPath)) {
                const std::string backupDir = inst::config::appDir + "/cheat_backups/" + FormatTitleId(target.titleId);
                std::filesystem::create_directories(backupDir);
                const std::string backupPath = backupDir + "/" + target.buildId + "." + std::to_string(armGetSystemTick()) + ".txt";
                std::filesystem::rename(targetPath, backupPath);
            }
            std::filesystem::rename(tempPath, targetPath);
            if (!WriteMetadata(target, entry)) {
                error = "Cheat installed, but its conflict metadata could not be saved.";
                return true;
            }
            return true;
        } catch (const std::exception& ex) {
            error = ex.what();
            return false;
        }
    }

    bool Remove(const Target& target, std::string& error)
    {
        error.clear();
        try {
            const std::string path = CheatPath(target);
            if (!std::filesystem::exists(path)) {
                const std::string metadataPath = MetadataPath(target);
                if (std::filesystem::exists(metadataPath))
                    std::filesystem::remove(metadataPath);
                return true;
            }
            if (!std::filesystem::remove(path)) {
                error = "Unable to remove the cheat file.";
                return false;
            }
            const std::string metadataPath = MetadataPath(target);
            if (std::filesystem::exists(metadataPath))
                std::filesystem::remove(metadataPath);
            return true;
        } catch (const std::exception& ex) {
            error = ex.what();
            return false;
        }
    }

    bool IsInstalled(const Target& target)
    {
        return target.titleId != 0 && IsHex16(target.buildId) && std::filesystem::exists(CheatPath(target));
    }

    bool GetInstalledMetadata(const Target& target, Entry& out)
    {
        out = {};
        try {
            std::ifstream input(MetadataPath(target), std::ios::binary);
            if (!input)
                return false;
            nlohmann::json json;
            input >> json;
            if (!json.is_object())
                return false;
            out.id = json.value("id", "");
            out.name = json.value("name", "Installed cheat");
            out.source = json.value("source", "unknown");
            ReadStringArray(json, "tags", out.tags);
            ReadStringArray(json, "conflict_groups", out.conflictGroups);
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
