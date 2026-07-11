#pragma once

#include <cstdint>
#include <string>
#include <vector>

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
    };

    struct InstalledTitle {
        std::uint64_t titleId = 0;
        std::string name;
    };

    struct BuildBundle {
        std::string buildId;
        std::string content;
        std::size_t entryCount = 0;
        std::vector<std::string> conflictGroups;
    };

    bool GetRunningTarget(Target& out, std::string& error);
    bool FetchExact(const Target& target, std::vector<Entry>& out, std::vector<std::string>& availableBuildIds, std::string& error);
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
