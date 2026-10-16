#pragma once
#include <pu/Plutonium>
#include <atomic>
#include <mutex>
#include <thread>
#include "util/cheat_service.hpp"

namespace inst::ui {
    class CheatsPage : public pu::ui::Layout {
    public:
        CheatsPage();
        ~CheatsPage();
        PU_SMART_CTOR(CheatsPage)
        void Open();
        void Poll();
        void Stop();
    private:
        enum class View { Games, Builds, Entries, Diagnostics, History };
        View view = View::Games;
        pu::ui::elm::Menu::Ref menu;
        pu::ui::elm::TextBlock::Ref heading, detail, status, hints;
        std::vector<inst::cheats::InstalledTitle> games;
        std::vector<inst::cheats::BuildBundle> builds;
        std::vector<inst::cheats::Entry> entries;
        std::vector<bool> selected;
        inst::cheats::Target running;
        std::size_t gameIndex = 0, buildIndex = 0;
        std::atomic_bool cancelled{false};
        std::thread worker;
        std::mutex resultMutex;
        std::function<void()> result;
        bool busy = false;
        bool cancellable = true;
        int detailOffset = 0;
        std::string extraDetail;
        void Run(std::function<std::function<void()>()> job, bool canCancel = true);
        void Scan();
        void LoadBuilds();
        void LoadEntries();
        void Rebuild();
        void ShowDetail();
        void InstallSelection();
        void OnInput(u64 down, u64 up, u64 held, pu::ui::Touch touch);
    };
}
