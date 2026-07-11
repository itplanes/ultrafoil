#pragma once
#include <pu/Plutonium>
#include "ui/bottomHint.hpp"
#include "ui/overflowText.hpp"

using namespace pu::ui::elm;
namespace inst::ui {
    class MainPage : public pu::ui::Layout
    {
        public:
            MainPage();
            PU_SMART_CTOR(MainPage)
            void installMenuItem_Click();
            void netInstallMenuItem_Click();
            void remoteInstallMenuItem_Click();
            void cheatsMenuItem_Click();
            void usbInstallMenuItem_Click();
            void hddInstallMenuItem_Click();
            void mtpInstallMenuItem_Click();
            void backupSaveDataMenuItem_Click();
            void settingsMenuItem_Click();
            void exitMenuItem_Click();
            void onInput(u64 Down, u64 Up, u64 Held, pu::ui::Touch Pos);
            Image::Ref awooImage;
            Image::Ref titleImage;
            TextBlock::Ref appVersionText;
            TextBlock::Ref timeText;
            TextBlock::Ref ipText;
            TextBlock::Ref sysLabelText;
            TextBlock::Ref sysFreeText;
            TextBlock::Ref sdLabelText;
            TextBlock::Ref sdFreeText;
            Rectangle::Ref sysBarBack;
            Rectangle::Ref sysBarFill;
            Rectangle::Ref sdBarBack;
            Rectangle::Ref sdBarFill;
            Rectangle::Ref netIndicator;
            Rectangle::Ref wifiBar1;
            Rectangle::Ref wifiBar2;
            Rectangle::Ref wifiBar3;
            Rectangle::Ref batteryOutline;
            Rectangle::Ref batteryFill;
            Rectangle::Ref batteryCap;
        private:
            bool appletFinished;
            bool updateFinished;
            bool touchActive = false;
            bool touchMoved = false;
            int touchStartX = 0;
            int touchStartY = 0;
            BottomHintTouchState bottomHintTouch;
            std::vector<BottomHintSegment> bottomHintSegments;
            TextBlock::Ref butText;
            Rectangle::Ref topRect;
            Rectangle::Ref botRect;
            Rectangle::Ref backupUserPickerRect;
            TextBlock::Ref backupUserPickerTitle;
            TextBlock::Ref backupUserPickerHint;
            pu::ui::elm::Menu::Ref optionMenu;
            pu::ui::elm::MenuItem::Ref installMenuItem;
            pu::ui::elm::MenuItem::Ref netInstallMenuItem;
            pu::ui::elm::MenuItem::Ref remoteInstallMenuItem;
            pu::ui::elm::MenuItem::Ref usbInstallMenuItem;
            pu::ui::elm::MenuItem::Ref hddInstallMenuItem;
            pu::ui::elm::MenuItem::Ref mtpInstallMenuItem;
            pu::ui::elm::MenuItem::Ref backupSaveDataMenuItem;
            pu::ui::elm::MenuItem::Ref settingsMenuItem;
            pu::ui::elm::MenuItem::Ref exitMenuItem;
            std::vector<Rectangle::Ref> mainGridTiles;
            std::vector<Image::Ref> mainGridIcons;
            std::vector<OverflowText::Ref> mainGridLabels;
            Rectangle::Ref mainGridHighlight;
            int selectedMainIndex = 0;
            void updateMainGridSelection();
            void updateMainGridLabelEffects(bool force);
            int getMainGridIndexFromTouch(int x, int y) const;
            int promptBackupUserSelection(const std::vector<std::string>& userLabels, int preferredIndex);
            void setBackupUserPickerVisible(bool visible);
            void activateSelectedMainItem();
            void showSelectedMainInfo();
    };
}
