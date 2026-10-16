#pragma once
#include <pu/Plutonium>
#include <switch.h>
#include "ui/mainPage.hpp"
#include "ui/netInstPage.hpp"
#include "ui/remoteInstPage.hpp"
#include "ui/sdInstPage.hpp"
#include "ui/usbInstPage.hpp"
#include "ui/hddInstPage.hpp"
#include "ui/instPage.hpp"
#include "ui/optionsPage.hpp"
#include "ui/cheatsPage.hpp"

namespace inst::ui {
    class MainApplication;
    extern MainApplication *mainApp;

    class MainApplication : public pu::ui::Application {
        public:
            using Application::Application;
            PU_SMART_CTOR(MainApplication)
            void OnLoad() override;
            void RefreshInputDevice(bool force = false);
            pu::ui::Layout::Ref GetCurrentLayout() const { return this->lyt; }
            MainPage::Ref mainPage;
            netInstPage::Ref netinstPage;
            remoteInstPage::Ref remoteinstPage;
            sdInstPage::Ref sdinstPage;
            usbInstPage::Ref usbinstPage;
            hddInstPage::Ref hddinstPage;
            instPage::Ref instpage;
            optionsPage::Ref optionspage;
            CheatsPage::Ref cheatsPage;
        private:
            AppletFocusState lastFocusState = AppletFocusState_InFocus;
    };
}
