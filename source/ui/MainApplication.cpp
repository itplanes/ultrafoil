#include "ui/MainApplication.hpp"
#include "util/lang.hpp"
#include "util/config.hpp"
#include "util/util.hpp"
#include <ctime>
#include <cstdio>
#include "switch.h"

#define COLOR(hex) pu::ui::Color::FromHex(hex)

namespace inst::ui {
    MainApplication *mainApp;

    void MainApplication::RefreshInputDevice(bool force) {
        const AppletFocusState focus = appletGetFocusState();
        const bool regainedFocus = (focus != this->lastFocusState) && (focus == AppletFocusState_InFocus);
        this->lastFocusState = focus;

        if (focus != AppletFocusState_InFocus)
            return;

        if (force || regainedFocus || !padIsConnected(&this->input_pad)) {
            padConfigureInput(8, HidNpadStyleSet_NpadStandard);
            padInitializeAny(&this->input_pad);
        }
    }

    void MainApplication::OnLoad() {
        mainApp = this;
        this->RefreshInputDevice(true);

        Language::Load();

        this->mainPage = MainPage::New();
        this->netinstPage = netInstPage::New();
        this->remoteinstPage = remoteInstPage::New();
        this->instpage = instPage::New();
        this->optionspage = optionsPage::New();
        this->mainPage->SetOnInput(std::bind(&MainPage::onInput, this->mainPage, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
        this->netinstPage->SetOnInput(std::bind(&netInstPage::onInput, this->netinstPage, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
        this->remoteinstPage->SetOnInput(std::bind(&remoteInstPage::onInput, this->remoteinstPage, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
        this->instpage->SetOnInput(std::bind(&instPage::onInput, this->instpage, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
        this->optionspage->SetOnInput(std::bind(&optionsPage::onInput, this->optionspage, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
        this->LoadLayout(this->mainPage);

        this->AddThread([this]() {
            this->RefreshInputDevice();
        });

        this->AddThread([this]() {
            static u64 lastTick = 0;
            const u64 now = armGetSystemTick();
            const u64 freq = armGetSystemTickFreq();
            if (lastTick != 0 && (now - lastTick) < freq)
                return;
            lastTick = now;

            std::string timeText = "--:--";
            if (R_SUCCEEDED(timeInitialize())) {
                u64 posix = 0;
                if (R_SUCCEEDED(timeGetCurrentTime(TimeType_LocalSystemClock, &posix))) {
                    std::time_t t = static_cast<std::time_t>(posix);
                    std::tm* local = std::localtime(&t);
                    char buf[16] = {0};
                    if (local && std::strftime(buf, sizeof(buf), "%I:%M %p", local) > 0)
                        timeText = buf;
                }
                timeExit();
            }

            bool internetUp = false;
            bool wifiConnected = false;
            u32 wifiStrength = 0;
            if (R_SUCCEEDED(nifmInitialize(NifmServiceType_User))) {
                NifmInternetConnectionStatus status = static_cast<NifmInternetConnectionStatus>(0);
                NifmInternetConnectionType type = static_cast<NifmInternetConnectionType>(0);
                if (R_SUCCEEDED(nifmGetInternetConnectionStatus(&type, &wifiStrength, &status))) {
                    internetUp = (status == NifmInternetConnectionStatus_Connected);
                    wifiConnected = internetUp && (type == NifmInternetConnectionType_WiFi);
                }
                nifmExit();
            }

            int batteryPct = -1;
            if (R_SUCCEEDED(psmInitialize())) {
                u32 pct = 0;
                if (R_SUCCEEDED(psmGetBatteryChargePercentage(&pct))) {
                    batteryPct = static_cast<int>(pct);
                }
                psmExit();
            }

            s64 systemTotal = 0;
            s64 systemFree = 0;
            s64 sdTotalBytes = 0;
            s64 sdFreeBytes = 0;
            if (R_SUCCEEDED(ncmInitialize())) {
                NcmContentStorage storage{};
                Result rc = ncmOpenContentStorage(&storage, NcmStorageId_BuiltInUser);
                if (R_SUCCEEDED(rc)) {
                    ncmContentStorageGetTotalSpaceSize(&storage, &systemTotal);
                    ncmContentStorageGetFreeSpaceSize(&storage, &systemFree);
                    ncmContentStorageClose(&storage);
                }
                rc = ncmOpenContentStorage(&storage, NcmStorageId_SdCard);
                if (R_SUCCEEDED(rc)) {
                    ncmContentStorageGetTotalSpaceSize(&storage, &sdTotalBytes);
                    ncmContentStorageGetFreeSpaceSize(&storage, &sdFreeBytes);
                    ncmContentStorageClose(&storage);
                }
                ncmExit();
            }

            const int cardWidth = 180;
            const int cardGap = 12;
            const int timeY = 24;
            const int iconY = 50;
            const int cardsTopY = 24;
            const int barY = 42;
            const int freeY = 52;
            const int netSize = 6;
            const int wifiBarW = 4;
            const int wifiBarGap = 2;
            const int wifiWidth = (wifiBarW * 3) + (wifiBarGap * 2);
            const int wifiMaxH = 10;
            const int batteryW = 24;
            const int batteryH = 12;
            const int batteryCapW = 3;
            const int iconGap = 6;
            const int iconsWidth = netSize + iconGap + wifiWidth + iconGap + batteryW + batteryCapW;
            auto applyStatus = [&](TextBlock::Ref timeBlock,
                                   TextBlock::Ref ipText,
                                   TextBlock::Ref sysLabel,
                                   TextBlock::Ref sysFree,
                                   Rectangle::Ref sysBarBack,
                                   Rectangle::Ref sysBarFill,
                                   TextBlock::Ref sdLabel,
                                   TextBlock::Ref sdFree,
                                   Rectangle::Ref sdBarBack,
                                   Rectangle::Ref sdBarFill,
                                   Rectangle::Ref netIndicator,
                                   Rectangle::Ref wifiBar1,
                                   Rectangle::Ref wifiBar2,
                                   Rectangle::Ref wifiBar3,
                                   Rectangle::Ref batteryOutline,
                                   Rectangle::Ref batteryFill,
                                   Rectangle::Ref batteryCap) {
                int right = 1280 - 10;
                int cardsRight = right;
                int timeX = right;
                int timeW = 0;
                if (timeBlock) {
                    timeBlock->SetText(timeText);
                    timeBlock->SetY(timeY);
                    timeW = timeBlock->GetTextWidth();
                    timeX = right - timeW;
                    timeBlock->SetX(timeX);
                    cardsRight = timeX - cardGap;
                }

                int iconsX = right - iconsWidth;
                if (timeW > 0) {
                    iconsX = timeX + (timeW - iconsWidth) / 2;
                    if (iconsX < 0)
                        iconsX = 0;
                }
                int netX = iconsX;
                int wifiX = netX + netSize + iconGap;
                int batteryX = wifiX + wifiWidth + iconGap;

                if (netIndicator) {
                    netIndicator->SetX(netX);
                    netIndicator->SetY(iconY + 2);
                    netIndicator->SetColor(internetUp ? COLOR("#4CD964FF") : COLOR("#FF3B30FF"));
                }

                if (wifiBar1 && wifiBar2 && wifiBar3) {
                    const int wifiBaseY = iconY + wifiMaxH;
                    wifiBar1->SetX(wifiX);
                    wifiBar1->SetY(wifiBaseY - 4);
                    wifiBar2->SetX(wifiX + wifiBarW + wifiBarGap);
                    wifiBar2->SetY(wifiBaseY - 7);
                    wifiBar3->SetX(wifiX + (wifiBarW + wifiBarGap) * 2);
                    wifiBar3->SetY(wifiBaseY - 10);
                    pu::ui::Color onColor = COLOR("#FFFFFFFF");
                    pu::ui::Color offColor = COLOR("#FFFFFF55");
                    const bool showWifi = wifiConnected && wifiStrength > 0;
                    wifiBar1->SetColor((showWifi && wifiStrength >= 1) ? onColor : offColor);
                    wifiBar2->SetColor((showWifi && wifiStrength >= 2) ? onColor : offColor);
                    wifiBar3->SetColor((showWifi && wifiStrength >= 3) ? onColor : offColor);
                    wifiBar1->SetVisible(true);
                    wifiBar2->SetVisible(true);
                    wifiBar3->SetVisible(true);
                }

                if (batteryOutline && batteryFill && batteryCap) {
                    batteryOutline->SetX(batteryX);
                    batteryOutline->SetY(iconY);
                    batteryOutline->SetWidth(batteryW);
                    batteryOutline->SetHeight(batteryH);
                    batteryCap->SetX(batteryX + batteryW + 1);
                    batteryCap->SetY(iconY + 3);
                    batteryCap->SetWidth(batteryCapW);
                    batteryCap->SetHeight(6);

                    int fillWidth = 0;
                    if (batteryPct >= 0) {
                        double ratio = static_cast<double>(batteryPct) / 100.0;
                        if (ratio < 0.0) ratio = 0.0;
                        if (ratio > 1.0) ratio = 1.0;
                        fillWidth = static_cast<int>((batteryW - 2) * ratio);
                        if (fillWidth < 2 && ratio > 0.0) fillWidth = 2;
                    }
                    batteryFill->SetX(batteryX + 1);
                    batteryFill->SetY(iconY + 1);
                    batteryFill->SetWidth(fillWidth);
                    batteryFill->SetHeight(batteryH - 2);
                    batteryFill->SetColor((batteryPct >= 0 && batteryPct <= 20) ? COLOR("#FF3B30FF") : COLOR("#4CD964FF"));
                }

                int sdX = cardsRight - cardWidth;
                int sysX = sdX - cardGap - cardWidth;

                if (ipText) {
                    std::string ipAddress = inst::util::getIPAddress();
                    if (ipAddress == "1.0.0.127") ipAddress = "--";
                    ipText->SetText("IP: " + ipAddress);
                    int ipWidth = ipText->GetTextWidth();
                    int ipX = sysX - cardGap - ipWidth;
                    if (ipX < 10) ipX = 10;
                    ipText->SetX(ipX);
                    ipText->SetY(cardsTopY);
                }

                if (sysLabel) {
                    sysLabel->SetX(sysX);
                    sysLabel->SetY(cardsTopY);
                }
                if (sysFree) {
                    sysFree->SetX(sysX);
                    sysFree->SetY(freeY);
                }
                if (sysBarBack) {
                    sysBarBack->SetX(sysX);
                    sysBarBack->SetY(barY);
                }
                if (sysBarFill) {
                    sysBarFill->SetX(sysX);
                    sysBarFill->SetY(barY);
                }

                if (sdLabel) {
                    sdLabel->SetX(sdX);
                    sdLabel->SetY(cardsTopY);
                }
                if (sdFree) {
                    sdFree->SetX(sdX);
                    sdFree->SetY(freeY);
                }
                if (sdBarBack) {
                    sdBarBack->SetX(sdX);
                    sdBarBack->SetY(barY);
                }
                if (sdBarFill) {
                    sdBarFill->SetX(sdX);
                    sdBarFill->SetY(barY);
                }
            };

            auto updateCards = [&](TextBlock::Ref sysLabel,
                                   TextBlock::Ref sysFree,
                                   Rectangle::Ref sysBarBack,
                                   Rectangle::Ref sysBarFill,
                                   TextBlock::Ref sdLabel,
                                   TextBlock::Ref sdFree,
                                   Rectangle::Ref sdBarBack,
                                   Rectangle::Ref sdBarFill) {
                if (sysLabel) sysLabel->SetText("System Memory");
                if (sdLabel) sdLabel->SetText("microSD Card");

                auto setCard = [&](TextBlock::Ref freeText, Rectangle::Ref barFill, s64 freeBytes, s64 totalBytes) {
                    if (freeText) {
                        char buf[64] = {0};
                        if (totalBytes > 0) {
                            double freeGb = static_cast<double>(freeBytes) / (1024.0 * 1024.0 * 1024.0);
                            std::snprintf(buf, sizeof(buf), "Free Space %.1f GB", freeGb);
                        } else {
                            std::snprintf(buf, sizeof(buf), "Free Space --");
                        }
                        freeText->SetText(buf);
                    }
                    if (barFill) {
                        int width = 0;
                        if (totalBytes > 0) {
                            double usedBytes = static_cast<double>(totalBytes - freeBytes);
                            if (usedBytes < 0.0) usedBytes = 0.0;
                            double ratio = usedBytes / static_cast<double>(totalBytes);
                            if (ratio < 0.0) ratio = 0.0;
                            if (ratio > 1.0) ratio = 1.0;
                            width = static_cast<int>(cardWidth * ratio);
                            if (width < 2 && ratio > 0.0) width = 2;
                        }
                        barFill->SetWidth(width);
                    }
                };

                setCard(sysFree, sysBarFill, systemFree, systemTotal);
                setCard(sdFree, sdBarFill, sdFreeBytes, sdTotalBytes);
            };

            auto applyAll = [&](auto& page) {
                applyStatus(page->timeText, page->ipText, page->sysLabelText, page->sysFreeText, page->sysBarBack, page->sysBarFill,
                            page->sdLabelText, page->sdFreeText, page->sdBarBack, page->sdBarFill,
                            page->netIndicator, page->wifiBar1, page->wifiBar2, page->wifiBar3,
                            page->batteryOutline, page->batteryFill, page->batteryCap);
                updateCards(page->sysLabelText, page->sysFreeText, page->sysBarBack, page->sysBarFill,
                            page->sdLabelText, page->sdFreeText, page->sdBarBack, page->sdBarFill);
            };

            applyAll(this->mainPage);
            applyAll(this->netinstPage);
            applyAll(this->remoteinstPage);
            applyAll(this->instpage);
            applyAll(this->optionspage);
        });
    }
}
