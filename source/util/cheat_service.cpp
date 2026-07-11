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
            if (!std::filesystem::exists(path))
                return true;
            if (!std::filesystem::remove(path)) {
                error = "Unable to remove the cheat file.";
                return false;
            }
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
}
