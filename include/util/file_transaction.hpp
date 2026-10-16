#pragma once
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>

namespace inst::files {
    // The journal is kept until both files have committed. An interrupted
    // transaction is rolled back before the next read/write of this pair.
    inline void WriteChecked(const std::string& path, const std::string& content) {
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(content.data(), content.size());
        output.flush();
        output.close();
        if (!output) throw std::runtime_error("Unable to write " + path);
    }

    inline void RecoverPair(const std::string& first, const std::string& second) {
        const std::string journal = first + ".transaction";
        if (!std::filesystem::exists(journal)) return;
        std::ifstream input(journal);
        bool hadFirst = false, hadSecond = false;
        if (!(input >> hadFirst >> hadSecond))
            throw std::runtime_error("Invalid recovery journal: " + journal);
        for (const auto& entry : {std::make_pair(first, hadFirst), std::make_pair(second, hadSecond)}) {
            if (std::filesystem::exists(entry.first + ".rollback")) {
                std::filesystem::remove(entry.first);
                std::filesystem::rename(entry.first + ".rollback", entry.first);
            } else if (!entry.second) {
                std::filesystem::remove(entry.first);
            }
            std::filesystem::remove(entry.first + ".pending");
        }
        std::filesystem::remove(journal);
    }

    using RenameFile = std::function<void(const std::string&, const std::string&)>;
    inline bool CommitPair(const std::string& first, const std::string& second,
                           const std::optional<std::string>& firstContent,
                           const std::optional<std::string>& secondContent,
                           std::string& error, RenameFile renameFile = {}) {
        error.clear();
        if (!renameFile) renameFile = [](const auto& from, const auto& to) { std::filesystem::rename(from, to); };
        try {
            RecoverPair(first, second);
            for (const auto& path : {first, second}) {
                if (std::filesystem::exists(path) && !std::filesystem::is_regular_file(path))
                    throw std::runtime_error("Not a regular file: " + path);
                std::filesystem::remove(path + ".rollback");
            }
            if (firstContent) WriteChecked(first + ".pending", *firstContent);
            if (secondContent) WriteChecked(second + ".pending", *secondContent);
            WriteChecked(first + ".transaction.pending", std::to_string(std::filesystem::exists(first)) + " " +
                         std::to_string(std::filesystem::exists(second)));
            std::filesystem::rename(first + ".transaction.pending", first + ".transaction");
            if (std::filesystem::exists(first)) renameFile(first, first + ".rollback");
            if (firstContent) renameFile(first + ".pending", first);
            if (std::filesystem::exists(second)) renameFile(second, second + ".rollback");
            if (secondContent) renameFile(second + ".pending", second);
            std::filesystem::remove(first + ".transaction");
            // A committed pair remains valid even if stale rollback cleanup fails.
            std::error_code ignored;
            for (const auto& path : {first, second}) std::filesystem::remove(path + ".rollback", ignored);
            return true;
        } catch (const std::exception& ex) {
            error = ex.what();
            try { RecoverPair(first, second); }
            catch (const std::exception& recovery) { error += "; recovery required: " + std::string(recovery.what()); }
            return false;
        }
    }
}
