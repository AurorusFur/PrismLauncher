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
class ResourceFolderModel;
class QLabel;
class QLineEdit;
class QListView;
class QListWidget;
class ResourceDownloadTask;
template <typename T>
class shared_qobject_ptr;

namespace ResourceDownload {
class ResourceModel;
}

// Big Picture variant of the resource download manager: a controller-native,
// full-screen browser that reuses the desktop downloader's backend (search
// models, version filters, download tasks) with couch-style presentation.
// ModFolderPage::downloadMods() and ShaderPackPage::downloadShaderPack() open
// this instead of the desktop ResourceDownloadDialog while Big Picture mode is
// active.
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

    void openForMods(BaseInstance* instance, ResourceFolderModel* mods);
    void openForShaderPacks(BaseInstance* instance, ResourceFolderModel* packs);
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
    enum class ResourceKind { Mods, ShaderPacks };

    void openInternal(ResourceKind kind, BaseInstance* instance, ResourceFolderModel* folder);
    void setupModel();  // (re)creates m_model for the current provider
    void onKeyboardCommitted();  // on-screen keyboard Done → run the search
    void onKeyboardClosed();     // keyboard closed → focus back on the results
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
    ResourceFolderModel* m_targetFolder = nullptr;  // owned by the instance, outlives the browser session
    ResourceDownload::ResourceModel* m_model = nullptr;  // points at one of the cached models below
    Provider m_provider = Provider::Modrinth;
    ResourceKind m_kind = ResourceKind::Mods;
    bool m_curseForgeAvailable = false;  // API key present and loaders supported

    // Models are kept per provider for the current instance and resource kind so
    // reopening the browser or toggling the provider shows previous results/icons
    // instantly instead of re-running the search. Dropped when instance or kind
    // changes; the id double-check guards against address reuse.
    ResourceDownload::ResourceModel* m_modrinthModel = nullptr;
    ResourceDownload::ResourceModel* m_flameModel = nullptr;
    BaseInstance* m_modelInstance = nullptr;
    QString m_modelInstanceId;
    ResourceKind m_modelKind = ResourceKind::Mods;

    QString m_lastHudKey;  // skip QLabel rich-text relayout when the hint didn't change

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
