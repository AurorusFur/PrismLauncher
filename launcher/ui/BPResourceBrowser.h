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

#include <QList>
#include <QWidget>
#include <memory>

#include "modplatform/ModIndex.h"

class BaseInstance;
class ModFolderModel;
class QLabel;
class QLineEdit;
class QListView;
class QListWidget;
class ResourceDownloadTask;
template <typename T>
class shared_qobject_ptr;

namespace ResourceDownload {
class ModModel;
}

// Big Picture variant of the mod download manager: a controller-native,
// full-screen browser that reuses the desktop downloader's backend (Modrinth
// search model, version filters, download tasks) with couch-style presentation.
// ModFolderPage::downloadMods() opens this instead of the desktop
// ResourceDownloadDialog while Big Picture mode is active.
//
// Controls: ↑↓ move through results (↑ from the top row jumps to search),
// A opens the version picker / installs the highlighted version, X focuses the
// search field (type with a keyboard, A/Enter searches), LB/RB page through
// results, B backs out (version card → search → close).
class BPResourceBrowser : public QWidget {
    Q_OBJECT

public:
    explicit BPResourceBrowser(QWidget* parent);
    ~BPResourceBrowser() override;

    void openForMods(BaseInstance* instance, ModFolderModel* mods);
    void closeBrowser();

    // Registered while Big Picture mode is active; pages use this to decide
    // between the desktop dialog and this browser.
    static BPResourceBrowser* activeInstance() { return s_instance; }
    static void setActiveInstance(BPResourceBrowser* browser) { s_instance = browser; }

    // Gamepad actions — routed from MainWindow while visible
    void navUp();
    void navDown();
    void doConfirm();       // A — open versions / install / commit search
    void doCancel();        // B — close version card / leave search / close browser
    void focusSearch();     // X
    void toggleProvider();  // Y — Modrinth ⇄ CurseForge
    void pageUp();          // LB
    void pageDown();        // RB

signals:
    void browserClosing();

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void changeEvent(QEvent*) override;

private:
    enum class Provider { Modrinth, CurseForge };

    void setupModel();  // (re)creates m_model for the current provider
    void updateTitle();
    void relayout();
    void applyTheme();
    void updateHud();
    void setStatus(const QString& text);
    void runSearch();
    void moveSelection(int delta);
    void openVersionsForRow(int row);
    void showVersionCard(int row);
    void hideVersionCard();
    void installVersion(int versionRow);
    void positionVersionCard();

    static inline BPResourceBrowser* s_instance = nullptr;

    BaseInstance* m_instance = nullptr;
    ModFolderModel* m_mods = nullptr;  // owned by the instance, outlives the browser session
    ResourceDownload::ModModel* m_model = nullptr;
    Provider m_provider = Provider::Modrinth;
    bool m_curseForgeAvailable = false;  // API key present and loaders supported

    int m_pendingVersionRow = -1;
    ModPlatform::IndexedPack::Ptr m_cardPack;
    QList<ModPlatform::IndexedVersion> m_cardVersions;
    QList<shared_qobject_ptr<ResourceDownloadTask>> m_tasks;  // keep running downloads alive

    // Main layout
    QLabel* m_titleLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLineEdit* m_search = nullptr;
    QListView* m_results = nullptr;
    QLabel* m_hudLabel = nullptr;

    // Version picker card
    QWidget* m_versionCard = nullptr;
    QLabel* m_versionTitle = nullptr;
    QListWidget* m_versionList = nullptr;
    QLabel* m_versionHud = nullptr;

    static constexpr int TITLE_H = 52;
    static constexpr int HUD_H = 36;
    static constexpr int SEARCH_H = 40;
    static constexpr int MARGIN = 24;
};
