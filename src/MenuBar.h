//
// MenuBar.h
//
#pragma once
#include <gui/MenuBar.h>

class MenuBar : public gui::MenuBar
{
    gui::SubMenu _subApp;
protected:
    void populateAppMenu()
    {
        auto& items = _subApp.getItems();
        items[0].initAsActionItem("Reset", 10);
        items[1].initAsSeparator();
        items[2].initAsActionItem("Run 50 Trials", 20);
        items[3].initAsSeparator();
        items[4].initAsQuitAppActionItem("Quit", "q");
    }
public:
    MenuBar()
    : gui::MenuBar(1)
    , _subApp(10, "App", 5)
    {
        populateAppMenu();
        _menus[0] = &_subApp;
    }
};
