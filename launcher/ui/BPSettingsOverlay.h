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
#include <QPointer>
#include <QScrollArea>
#include <QWidget>
#include <memory>

#include "IBigPicturePrompt.h"

class QAction;
class QEventLoop;
class QTabWidget;
class QTimer;
class BaseInstance;
class BasePage;
class InstancePageProvider;
class PageContainer;

// Full-screen settings overlay embedded inside MainWindow.
//
// Layout: left sidebar (page list) | right content (native page widget)
//
// Mode::TabBar    — sidebar focused; ↑↓ select page, →/A enter content, B close.
// Mode::Content   — content focused; ↑↓←→ navigate, X opens action menu, B back.
// Mode::ActionMenu— floating action popup is open; ↑↓ navigate, A select, B cancel.
class BPSettingsOverlay : public QWidget, public IBigPicturePrompt {
    Q_OBJECT

public:
    enum class Mode { TabBar, Content, ActionMenu, PromptCard };

    explicit BPSettingsOverlay(QWidget* parent = nullptr);
    ~BPSettingsOverlay();

    void open(BaseInstance* instance);
    void closeOverlay();

    Mode mode() const { return m_mode; }

    // Gamepad actions — called from MainWindow routing slots
    void tabLeft();                  // LB → previous page (both modes)
    void tabRight();                 // RB → next page (both modes)
    void navUp();                    // ↑ — routed to the correct widget for current mode
    void navDown();                  // ↓
    void navLeft();                  // ←
    void navRight();                 // → (enters content from sidebar)
    void enterContent();             // A in sidebar → enter content
    void exitToTabBar();             // B in content → back to sidebar
    void doConfirm();                // A in content → Return key
    void doTabKey();                 // Tab key to next focusable widget
    void triggerPrimaryAction();     // Y → Download/Add/Install
    void triggerToggleAction();      // Start → Toggle/Join
    void showActionMenu();           // X → floating action popup with all page actions
    void confirmActionMenu();        // A while ActionMenu open
    void dismissActionMenu();        // B while ActionMenu open

    // IBigPicturePrompt — controller-friendly inline confirmation dialog
    int execPrompt(const QString& title, const QString& msg, const QStringList& buttons, int defaultIndex = 0) override;
    void confirmPrompt();            // A while PromptCard open
    void cancelPrompt();             // B while PromptCard open

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
    QList<QWidget*> orderedContentWidgets() const;  // controller-usable widgets in visual order
    void focusNextInContent(bool forward);  // wrap-around traversal of controller-usable widgets
    QWidget* contentFocusWidget() const;  // focused widget inside content, or container fallback
    PageContainer* currentContainer() const;  // whichever of m_container / m_globalContainer is active
    void switchContainerForPage(int pageIndex);
    void sidebarNavUp();    // move to previous selectable sidebar row (skips headers)
    void sidebarNavDown();  // move to next selectable sidebar row
    QTabWidget* innerTabWidget() const;  // current page's own QTabWidget, if any
    bool cycleInnerTab(int delta);       // LB/RB within a tabbed page; false if no tabs
    void enterValueEdit(QWidget* w);     // spinbox edit mode: ↑↓ adjust value
    void leaveValueEdit();

    // Lifecycle
    void rebuild(BaseInstance* instance);
    void teardown();
    void switchToTabIndex(int index, bool focusContent = false);
    void setMode(Mode m);
    void updateHud();
    void updateTitle();  // "Instance › Page" breadcrumb in the title bar
    void updateFocusRing();  // reposition the focus highlight over the focused widget
    void applyTheme();
    void relayout();
    void focusPageContent();

    // Action popup helpers
    void buildActionPopup();
    void positionActionPopup();
    QList<QAction*> gatherPageActions() const;

    // Prompt card helpers
    void buildPromptCard();
    void positionPromptCard();

    // Help widget helpers
    void buildHelpWidget();
    void showHelp(bool show);
    QString helpHtml() const;

    Mode m_mode = Mode::TabBar;
    int  m_currentTab = 0;
    bool m_helpShowing = false;
    bool m_globalSettingsVisited = false;
    BaseInstance* m_instance = nullptr;

    // Sidebar + title
    QLabel*          m_titleLabel    = nullptr;
    QListWidget*     m_sidebar       = nullptr;
    QList<BasePage*> m_filteredPages;

    // Page content — instance pages
    PageContainer* m_container      = nullptr;
    // Page content — global launcher settings
    PageContainer* m_globalContainer = nullptr;
    int            m_instancePageCount = 0;  // split point in m_filteredPages

    QScrollArea*   m_helpWidget = nullptr;
    QLabel*        m_hudLabel   = nullptr;

    // Controller focus indicator (see FocusRingWidget in the .cpp)
    QWidget* m_focusRing = nullptr;
    QTimer*  m_ringTimer = nullptr;

    // Spinbox currently in value-edit mode (A to enter, A/B to leave), or null
    QPointer<QWidget> m_editWidget;

    // Translucent scrim shown behind the action popup / prompt card
    QWidget* m_modalScrim = nullptr;

    // Action popup (shown over content area for Mode::ActionMenu)
    QWidget*         m_actionCard  = nullptr;
    QLabel*          m_actionTitle = nullptr;
    QListWidget*     m_actionList  = nullptr;
    QLabel*          m_actionHud   = nullptr;
    QList<QAction*>  m_currentActions;

    // Prompt card (controller-friendly confirmation dialog, Mode::PromptCard)
    QWidget*     m_promptCard    = nullptr;
    QLabel*      m_promptTitle   = nullptr;
    QLabel*      m_promptMessage = nullptr;
    QListWidget* m_promptList    = nullptr;
    QLabel*      m_promptHud     = nullptr;
    QEventLoop*  m_promptLoop    = nullptr;
    int          m_promptResult  = -1;

    std::shared_ptr<InstancePageProvider> m_provider;

    static constexpr int TITLE_H   = 52;
    static constexpr int HUD_H     = 36;
    static constexpr int SIDEBAR_W = 230;
    // Values stored in Qt::UserRole of sidebar items:
    //   >= 0           → index into m_filteredPages
    //   HEADER_ROLE    → section header / separator (not selectable)
    //   HELP_ROW_ROLE  → Controller Help entry
    static constexpr int HEADER_ROLE   = -1;
    static constexpr int HELP_ROW_ROLE = -2;
};
