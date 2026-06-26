// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2024 Prism Launcher Contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QLabel>
#include <QList>
#include <QListWidget>
#include <QWidget>
#include <memory>

class BaseInstance;
class BasePage;
class InstancePageProvider;
class PageContainer;

// Full-screen settings overlay embedded inside MainWindow.
//
// Layout: left sidebar (page list) | right content (native page widget)
//
// Mode::TabBar  — sidebar has focus; ↑↓ select page, → or A enter content, B close.
// Mode::Content — content has focus; ↑↓←→ navigate, B/← back to sidebar, X/Y/Start = actions.
class BPSettingsOverlay : public QWidget {
    Q_OBJECT

public:
    enum class Mode { TabBar, Content };

    explicit BPSettingsOverlay(QWidget* parent = nullptr);
    ~BPSettingsOverlay();

    void open(BaseInstance* instance);
    void closeOverlay();

    Mode mode() const { return m_mode; }

    // Gamepad actions — called from MainWindow routing slots
    void tabLeft();              // LB or ↑ in sidebar → previous page
    void tabRight();             // RB or ↓ in sidebar → next page
    void enterContent();         // A or → in sidebar → enter content mode
    void exitToTabBar();         // B in content → back to sidebar
    void doConfirm();            // A in content → Return key
    void doTabKey();             // Tab key to next focusable widget (fallback)
    void triggerPrimaryAction();    // Y → Download/Add
    void triggerSecondaryAction();  // X → Remove/Delete
    void triggerToggleAction();     // Start → Toggle/Join

signals:
    void overlayClosing();

protected:
    void paintEvent(QPaintEvent*) override;
    void showEvent(QShowEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void changeEvent(QEvent*) override;

private:
    enum class PageCategory { ExternalResource, Version, WorldList, Servers, Screenshots, Settings, Log, Other };
    PageCategory currentPageCategory() const;
    bool triggerPageAction(const QString& actionName);
    void sendKeyToFocused(Qt::Key key);

    void rebuild(BaseInstance* instance);
    void teardown();
    void switchToTabIndex(int index, bool focusContent = false);
    void setMode(Mode m);
    void updateHud();
    void applyTheme();
    void relayout();
    void focusPageContent();

    Mode m_mode = Mode::TabBar;
    int  m_currentTab = 0;
    BaseInstance* m_instance = nullptr;

    QLabel*      m_titleLabel = nullptr;
    QListWidget* m_sidebar    = nullptr;
    QList<BasePage*> m_filteredPages;  // subset of pages actually shown in sidebar

    PageContainer* m_container = nullptr;
    QLabel*        m_hudLabel  = nullptr;

    std::shared_ptr<InstancePageProvider> m_provider;

    static constexpr int TITLE_H  = 52;
    static constexpr int HUD_H    = 36;
    static constexpr int SIDEBAR_W = 230;
};
