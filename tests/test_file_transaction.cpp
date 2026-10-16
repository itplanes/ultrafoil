#include "util/file_transaction.hpp"
#include <cassert>
#include <sstream>

static std::string read(const std::string& path) {
    std::ifstream in(path); std::stringstream contents; contents << in.rdbuf(); return contents.str();
}

int main() {
    const std::string root = ".tmp/file-transaction-test";
    std::filesystem::remove_all(root);
    const auto file = root + "/code.txt", meta = root + "/meta/state.json";
    inst::files::WriteChecked(file, "original");
    inst::files::WriteChecked(meta, "original metadata");
    std::string error;
    assert(!inst::files::CommitPair(file, meta, "new", "new metadata", error,
        [&](const auto& from, const auto& to) {
            if (to == meta) throw std::runtime_error("Simulated metadata commit failure");
            std::filesystem::rename(from, to);
        }));
    assert(read(file) == "original" && read(meta) == "original metadata");
    assert(inst::files::CommitPair(file, meta, "new", "new metadata", error));
    assert(read(file) == "new" && read(meta) == "new metadata");
    // Simulate process loss after the payload has changed, before metadata commit.
    inst::files::WriteChecked(file + ".transaction", "1 1");
    std::filesystem::rename(file, file + ".rollback");
    inst::files::WriteChecked(file, "interrupted");
    inst::files::RecoverPair(file, meta);
    assert(read(file) == "new" && read(meta) == "new metadata");
    assert(inst::files::CommitPair(file, meta, std::nullopt, std::nullopt, error));
    assert(!std::filesystem::exists(file) && !std::filesystem::exists(meta));
    std::filesystem::remove_all(root);
}
