//
// MainWindow.h
//
#pragma once
#include <gui/Window.h>
#include "MenuBar.h"
#include "MainView.h"

class MainWindow : public gui::Window
{
protected:
    MenuBar _mainMenuBar;
    MainView _mainView;

    bool onActionItem(gui::ActionItemDescriptor& aiDesc) override
    {
        auto [menuID, firstSubMenuID, lastSubMenuID, actionID] = aiDesc.getIDs();
        if (menuID == 10 && actionID == 10) // "Reset" in the App menu
        {
            _mainView.getViewGNSS().reset();
            return true;
        }
        if (menuID == 10 && actionID == 20) // "Run 50 Trials" in the App menu
        {
            _mainView.getViewGNSS().runBatchExperiment(50);
            return true;
        }
        return false;
    }

public:
    MainWindow()
    : gui::Window(gui::Size(1100, 650))
    {
        setTitle("GNSS Positioning");
        _mainMenuBar.setAsMain(this);
        setCentralView(&_mainView);
    }
};
