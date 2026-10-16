#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <atomic>

namespace inst::cheats {
    struct Target {
        std::uint64_t titleId = 0;
        std::string buildId;
    };

    struct Entry {
        std::string id;
        std::string name;
        std::string content;
        std::string source;
        std::vector<std::string> tags;
        std::vector<std::string> conflictGroups;
        std::string role = "normal";
        std::vector<std::string> selectedIds;
    };

    struct InstalledTitle {
        std::uint64_t titleId = 0;
        std::string name;
        std::uint32_t version = 0;
    };

    struct BuildBundle {
        std::string buildId;
        std::string content;
        std::size_t entryCount = 0;
        std::uint32_t version = 0;
        std::string versionLabel;
        std::vector<std::string> attributions;
        std::vector<std::string> conflictGroups;
        std::string compatibility = "unknown";
        bool versionKnown = false;
    };

    bool GetRunningTarget(Target& out, std::string& error);
    bool FetchExact(const Target& target, std::vector<Entry>& out, std::vector<std::string>& availableBuildIds, std::string& error, const std::atomic_bool* cancel = nullptr);
    bool FetchBuildIndex(const InstalledTitle& title, const std::string& actualBuildId, std::vector<BuildBundle>& out, std::string& error, const std::atomic_bool* cancel = nullptr);
    std::string Diagnostics(const std::atomic_bool* cancel = nullptr);
    bool ListInstalledTitles(std::vector<InstalledTitle>& out, std::string& error);
    bool FetchAllBuilds(std::uint64_t titleId, std::vector<BuildBundle>& out, std::string& error);
    bool InstallAllBuilds(std::uint64_t titleId, const std::vector<BuildBundle>& builds, std::size_t& installed, std::string& error);
    bool RemoveAllBuilds(std::uint64_t titleId, const std::vector<BuildBundle>& builds, std::size_t& removed, std::string& error);
    bool Install(const Target& target, const Entry& entry, std::string& error);
    bool Remove(const Target& target, std::string& error);
    bool IsInstalled(const Target& target);
    bool GetInstalledMetadata(const Target& target, Entry& out);
    std::vector<std::string> FindConflicts(const Entry& installed, const Entry& candidate);
    std::string FormatTags(const Entry& entry);
    std::string FormatTitleId(std::uint64_t titleId);
}
