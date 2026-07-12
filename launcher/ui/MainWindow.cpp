// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
 *  Copyright (C) 2023 TheKodeToad <TheKodeToad@proton.me>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Authors: Andrew Okin
 *               Peterix
 *               Orochimarufan <orochimarufan.x3@gmail.com>
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#include "Application.h"
#include "BuildConfig.h"
#include "FileSystem.h"

#include "MainWindow.h"
#include "ui_MainWindow.h"

#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QUrlQuery>
#include <QVariant>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QShortcut>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>
#include <QWidgetAction>
#include <memory>

#include <BaseInstance.h>
#include <BuildConfig.h>
#include <DesktopServices.h>
#include <InstanceList.h>
#include <MMCZip.h>
#include <icons/IconList.h>
#include <java/JavaInstallList.h>
#include <java/JavaUtils.h>
#include <launch/LaunchTask.h>
#include <minecraft/MinecraftInstance.h>
#include <minecraft/auth/AccountList.h>
#include <net/ApiDownload.h>
#include <net/NetJob.h>
#include <news/NewsChecker.h>
#include <tools/BaseProfiler.h>
#include <updater/ExternalUpdater.h>
#include "InstanceWindow.h"

#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QTimer>

#include "ui/BPHud.h"
#include "ui/BPStyle.h"
#include "ui/BPOptionsMenu.h"
#include "ui/GuiUtil.h"
#include "ui/ViewLogWindow.h"
#include "ui/dialogs/AboutDialog.h"
#include "ui/dialogs/CopyInstanceDialog.h"
#include "ui/dialogs/CreateShortcutDialog.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/ExportInstanceDialog.h"
#include "ui/dialogs/ExportPackDialog.h"
#include "ui/dialogs/IconPickerDialog.h"
#include "ui/dialogs/ImportResourceDialog.h"
#include "ui/dialogs/NewInstanceDialog.h"
#include "ui/dialogs/NewsDialog.h"
#include "ui/dialogs/ProgressDialog.h"
#include "ui/dialogs/skins/SkinManageDialog.h"
#include "ui/instanceview/InstanceDelegate.h"
#include "ui/instanceview/InstanceProxyModel.h"
#include "ui/instanceview/InstanceView.h"
#include "ui/themes/ITheme.h"
#include "ui/themes/ThemeManager.h"
#include "ui/widgets/LabeledToolButton.h"

#include "minecraft/PackProfile.h"
#include "minecraft/VersionFile.h"
#include "minecraft/WorldList.h"
#include "minecraft/mod/ModFolderModel.h"
#include "minecraft/mod/ResourcePackFolderModel.h"
#include "minecraft/mod/ShaderPackFolderModel.h"
#include "minecraft/mod/TexturePackFolderModel.h"
#include "minecraft/mod/tasks/LocalResourceParse.h"

#include "modplatform/ModIndex.h"
#include "modplatform/flame/FlameAPI.h"
#include "modplatform/flame/FlameModIndex.h"

#include "KonamiCode.h"

#include "InstanceCopyTask.h"
#include "InstanceDirUpdate.h"

#include "Json.h"

#include "MMCTime.h"

namespace {
QString profileInUseFilter(const QString& profile, bool used)
{
    if (used) {
        return QObject::tr("%1 (in use)").arg(profile);
    } else {
        return profile;
    }
}
}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    setWindowIcon(APPLICATION->logo());
    setWindowTitle(APPLICATION->applicationDisplayName());
#ifndef QT_NO_ACCESSIBILITY
    setAccessibleName(BuildConfig.LAUNCHER_DISPLAYNAME);
#endif

    // instance toolbar stuff
    {
        // Qt doesn't like vertical moving toolbars, so we have to force them...
        // See https://github.com/PolyMC/PolyMC/issues/493
        connect(ui->instanceToolBar, &QToolBar::orientationChanged,
                [this](Qt::Orientation) { ui->instanceToolBar->setOrientation(Qt::Vertical); });

        // if you try to add a widget to a toolbar in a .ui file
        // qt designer will delete it when you save the file >:(
        changeIconButton = new LabeledToolButton(this);
        changeIconButton->setObjectName(QStringLiteral("changeIconButton"));
        changeIconButton->setIcon(QIcon::fromTheme("news"));
        changeIconButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        connect(changeIconButton, &QToolButton::clicked, this, &MainWindow::on_actionChangeInstIcon_triggered);
        ui->instanceToolBar->insertWidgetBefore(ui->actionLaunchInstance, changeIconButton);

        renameButton = new LabeledToolButton(this);
        renameButton->setObjectName(QStringLiteral("renameButton"));
        renameButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        connect(renameButton, &QToolButton::clicked, this, &MainWindow::on_actionRenameInstance_triggered);
        ui->instanceToolBar->insertWidgetBefore(ui->actionLaunchInstance, renameButton);

        ui->instanceToolBar->insertSeparator(ui->actionLaunchInstance);

        // restore the instance toolbar settings
        const auto setting_name = QString("WideBarVisibility_%1").arg(ui->instanceToolBar->objectName());
        instanceToolbarSetting = APPLICATION->settings()->getOrRegisterSetting(setting_name);

        ui->instanceToolBar->setVisibilityState(QByteArray::fromBase64(instanceToolbarSetting->get().toString().toUtf8()));

        ui->instanceToolBar->addContextMenuAction(ui->newsToolBar->toggleViewAction());
        ui->instanceToolBar->addContextMenuAction(ui->instanceToolBar->toggleViewAction());
        ui->instanceToolBar->addContextMenuAction(ui->actionToggleStatusBar);
        ui->instanceToolBar->addContextMenuAction(ui->actionLockToolbars);
    }

    // set the menu for the folders help, accounts, and export tool buttons
    {
        auto foldersMenuButton = dynamic_cast<QToolButton*>(ui->mainToolBar->widgetForAction(ui->actionFoldersButton));
        ui->actionFoldersButton->setMenu(ui->foldersMenu);
        foldersMenuButton->setPopupMode(QToolButton::InstantPopup);

        helpMenuButton = dynamic_cast<QToolButton*>(ui->mainToolBar->widgetForAction(ui->actionHelpButton));
        ui->actionHelpButton->setMenu(new QMenu(this));
        ui->actionHelpButton->menu()->addActions(ui->helpMenu->actions());
        ui->actionHelpButton->menu()->removeAction(ui->actionCheckUpdate);
        helpMenuButton->setPopupMode(QToolButton::InstantPopup);

        auto accountMenuButton = dynamic_cast<QToolButton*>(ui->mainToolBar->widgetForAction(ui->actionAccountsButton));
        accountMenuButton->setPopupMode(QToolButton::InstantPopup);

        auto exportInstanceMenu = new QMenu(this);
        exportInstanceMenu->addAction(ui->actionExportInstanceZip);
        exportInstanceMenu->addAction(ui->actionExportInstanceMrPack);
        exportInstanceMenu->addAction(ui->actionExportInstanceFlamePack);
        ui->actionExportInstance->setMenu(exportInstanceMenu);
    }

    // hide, disable and show stuff
    {
        ui->actionReportBug->setVisible(!BuildConfig.BUG_TRACKER_URL.isEmpty());
        ui->actionMATRIX->setVisible(!BuildConfig.MATRIX_URL.isEmpty());
        ui->actionDISCORD->setVisible(!BuildConfig.DISCORD_URL.isEmpty());
        ui->actionREDDIT->setVisible(!BuildConfig.SUBREDDIT_URL.isEmpty());

        ui->actionCheckUpdate->setVisible(APPLICATION->updaterEnabled());

#ifndef Q_OS_MAC
        ui->actionAddToPATH->setVisible(false);
#endif

        // disabled until we have an instance selected
        ui->instanceToolBar->setEnabled(false);
        setInstanceActionsEnabled(false);

        // add a close button at the end of the main toolbar when running on gamescope / steam deck
        // this is only needed on gamescope because it defaults to an X11/XWayland session and
        // does not implement decorations
        if (qgetenv("XDG_CURRENT_DESKTOP") == "gamescope") {
            ui->mainToolBar->addAction(ui->actionCloseWindow);
        }

        ui->actionViewJavaFolder->setEnabled(BuildConfig.JAVA_DOWNLOADER_ENABLED);
    }

    {  // logs viewing
        connect(ui->actionViewLog, &QAction::triggered, this, [] { APPLICATION->showLogWindow(); });
    }

    // add the toolbar toggles to the view menu
    ui->viewMenu->addAction(ui->instanceToolBar->toggleViewAction());
    ui->viewMenu->addAction(ui->newsToolBar->toggleViewAction());

    updateThemeMenu();
    updateMainToolBar();
    // OSX magic.
    setUnifiedTitleAndToolBarOnMac(true);

    // Global shortcuts
    {
        // you can't set QKeySequence::StandardKey shortcuts in qt designer >:(
        ui->actionAddInstance->setShortcut(QKeySequence::New);
        ui->actionSettings->setShortcut(QKeySequence::Preferences);
        ui->actionUndoTrashInstance->setShortcut(QKeySequence::Undo);
        ui->actionDeleteInstance->setShortcuts({ QKeySequence(tr("Backspace")), QKeySequence::Delete });
        ui->actionCloseWindow->setShortcut(QKeySequence::Close);
        connect(ui->actionCloseWindow, &QAction::triggered, APPLICATION, &Application::closeCurrentWindow);

        // FIXME: This is kinda weird. and bad. We need some kind of managed shutdown.
        auto q = new QShortcut(QKeySequence::Quit, this);
        connect(q, &QShortcut::activated, APPLICATION, &Application::quit);
    }

    // Konami Code
    {
        secretEventFilter = new KonamiCode(this);
        connect(secretEventFilter, &KonamiCode::triggered, this, &MainWindow::konamiTriggered);
    }

    // Add the news label to the news toolbar.
    {
        m_newsChecker.reset(new NewsChecker(APPLICATION->network(), BuildConfig.NEWS_RSS_URL));
        newsLabel = new QToolButton();
        newsLabel->setIcon(QIcon::fromTheme("news"));
        newsLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        newsLabel->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        newsLabel->setFocusPolicy(Qt::NoFocus);
        ui->newsToolBar->insertWidget(ui->actionMoreNews, newsLabel);

        connect(newsLabel, &QAbstractButton::clicked, this, &MainWindow::newsButtonClicked);
        connect(m_newsChecker.get(), &NewsChecker::newsLoaded, this, &MainWindow::updateNewsLabel);
        updateNewsLabel();
    }

    // Create the instance list widget
    {
        view = new InstanceView(ui->centralWidget);

        view->setSelectionMode(QAbstractItemView::SingleSelection);
        m_listDelegate = new ListViewDelegate(this);
        auto delegate = m_listDelegate;
        view->setItemDelegate(m_listDelegate);
        view->setFrameShape(QFrame::NoFrame);
        // do not show ugly blue border on the mac
        view->setAttribute(Qt::WA_MacShowFocusRect, false);
        connect(delegate, &ListViewDelegate::textChanged, this, [this](QString before, QString after) {
            if (auto newRoot = askToUpdateInstanceDirName(m_selectedInstance, before, after, this); !newRoot.isEmpty()) {
                auto oldID = m_selectedInstance->id();
                auto newID = QFileInfo(newRoot).fileName();
                QString origGroup(APPLICATION->instances()->getInstanceGroup(oldID));
                bool syncGroup = origGroup != GroupId() && oldID != newID;
                if (syncGroup)
                    APPLICATION->instances()->setInstanceGroup(oldID, GroupId());

                refreshInstances();
                setSelectedInstanceById(newID);

                if (syncGroup)
                    APPLICATION->instances()->setInstanceGroup(newID, origGroup);
            }
        });

        view->installEventFilter(this);
        view->setContextMenuPolicy(Qt::CustomContextMenu);
        // Keyboard routing for the settings overlay (arrow keys map to gamepad nav slots)
        qApp->installEventFilter(this);
        connect(view, &QWidget::customContextMenuRequested, this, &MainWindow::showInstanceContextMenu);
        connect(view, &InstanceView::droppedURLs, this, &MainWindow::processURLs, Qt::QueuedConnection);

        proxymodel = new InstanceProxyModel(this);
        proxymodel->setSourceModel(APPLICATION->instances());
        proxymodel->sort(0);
        connect(proxymodel, &InstanceProxyModel::dataChanged, this, &MainWindow::instanceDataChanged);

        view->setModel(proxymodel);
        view->setSourceOfGroupCollapseStatus(
            [](const QString& groupName) -> bool { return APPLICATION->instances()->isGroupCollapsed(groupName); });
        connect(view, &InstanceView::groupStateChanged, APPLICATION->instances(), &InstanceList::on_GroupStateChanged);
        ui->horizontalLayout->addWidget(view);
    }
    // The cat background
    {
        // set the cat action priority here so you can still see the action in qt designer
        ui->actionCAT->setPriority(QAction::LowPriority);
        bool cat_enable = APPLICATION->settings()->get("TheCat").toBool();
        ui->actionCAT->setChecked(cat_enable);
        connect(ui->actionCAT, &QAction::toggled, this, &MainWindow::onCatToggled);
        connect(APPLICATION, &Application::currentCatChanged, this, &MainWindow::onCatChanged);
        setCatBackground(cat_enable);
    }

    // Togglable status bar
    {
        bool statusBarVisible = APPLICATION->settings()->get("StatusBarVisible").toBool();
        ui->actionToggleStatusBar->setChecked(statusBarVisible);
        connect(ui->actionToggleStatusBar, &QAction::toggled, this, &MainWindow::setStatusBarVisibility);
        setStatusBarVisibility(statusBarVisible);
    }

    // Lock toolbars
    {
        bool toolbarsLocked = APPLICATION->settings()->get("ToolbarsLocked").toBool();
        ui->actionLockToolbars->setChecked(toolbarsLocked);
        connect(ui->actionLockToolbars, &QAction::toggled, this, &MainWindow::lockToolbars);
        lockToolbars(toolbarsLocked);
    }
    // start instance when double-clicked
    connect(view, &InstanceView::activated, this, &MainWindow::instanceActivated);

    // track the selection -- update the instance toolbar
    connect(view->selectionModel(), &QItemSelectionModel::currentChanged, this, &MainWindow::instanceChanged);

    // track icon changes and update the toolbar!
    connect(APPLICATION->icons(), &IconList::iconUpdated, this, &MainWindow::iconUpdated);

    // model reset -> selection is invalid. All the instance pointers are wrong.
    connect(APPLICATION->instances(), &InstanceList::dataIsInvalid, this, &MainWindow::selectionBad);

    // handle newly added instances
    connect(APPLICATION->instances(), &InstanceList::instanceSelectRequest, this, &MainWindow::instanceSelectRequest);

    // When the global settings page closes, we want to know about it and update our state
    connect(APPLICATION, &Application::globalSettingsApplied, this, &MainWindow::globalSettingsClosed);

    m_statusLeft = new QLabel(tr("No instance selected"), this);
    m_statusCenter = new QLabel(tr("Total playtime: 0s"), this);
    statusBar()->addPermanentWidget(m_statusLeft, 1);
    statusBar()->addPermanentWidget(m_statusCenter, 0);

    // Add "manage accounts" button, right align
    QWidget* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui->mainToolBar->insertWidget(ui->actionAccountsButton, spacer);

    // Use undocumented property... https://stackoverflow.com/questions/7121718/create-a-scrollbar-in-a-submenu-qt
    ui->accountsMenu->setStyleSheet("QMenu { menu-scrollable: 1; }");

    repopulateAccountsMenu();

    // Update the menu when the active account changes.
    // Shouldn't have to use lambdas here like this, but if I don't, the compiler throws a fit.
    // Template hell sucks...
    connect(APPLICATION->accounts(), &AccountList::defaultAccountChanged, [this] { defaultAccountChanged(); });
    connect(APPLICATION->accounts(), &AccountList::listActivityChanged, [this] { defaultAccountChanged(); });
    connect(APPLICATION->accounts(), &AccountList::listChanged, [this] { defaultAccountChanged(); });

    // Show initial account
    defaultAccountChanged();

    // TODO: refresh accounts here?
    // auto accounts = APPLICATION->accounts();

    // load the news
    {
        m_newsChecker->reloadNews();
        updateNewsLabel();
    }

    if (APPLICATION->updaterEnabled()) {
        bool updatesAllowed = APPLICATION->updatesAreAllowed();
        updatesAllowedChanged(updatesAllowed);

        connect(ui->actionCheckUpdate, &QAction::triggered, this, &MainWindow::checkForUpdates);

        // set up the updater object.
        auto updater = APPLICATION->updater();

        if (updater) {
            connect(updater, &ExternalUpdater::canCheckForUpdatesChanged, this, &MainWindow::updatesAllowedChanged);
        }
    }

    connect(ui->actionUndoTrashInstance, &QAction::triggered, this, &MainWindow::undoTrashInstance);

    setSelectedInstanceById(APPLICATION->settings()->get("SelectedInstance").toString());

    // removing this looks stupid
    view->setFocus();

    // Defer Big Picture mode so it runs after restoreGeometry() in Application::showMainWindow()
    QTimer::singleShot(0, this, &MainWindow::applyBigPictureMode);

    retranslateUi();
}

// macOS always has a native menu bar, so these fixes are not applicable
// Other systems may or may not have a native menu bar (most do not - it seems like only Ubuntu Unity does)
#ifndef Q_OS_MAC
void MainWindow::keyReleaseEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Alt && !APPLICATION->settings()->get("MenuBarInsteadOfToolBar").toBool())
        ui->menuBar->setVisible(!ui->menuBar->isVisible());
    else
        QMainWindow::keyReleaseEvent(event);
}
#endif

void MainWindow::retranslateUi()
{
    if (m_selectedInstance) {
        m_statusLeft->setText(m_selectedInstance->getStatusbarDescription());
    } else {
        m_statusLeft->setText(tr("No instance selected"));
    }

    ui->retranslateUi(this);

    MinecraftAccountPtr defaultAccount = APPLICATION->accounts()->defaultAccount();
    if (defaultAccount) {
        auto profileLabel = profileInUseFilter(defaultAccount->displayName(), defaultAccount->isInUse());
        ui->actionAccountsButton->setText(profileLabel);
    }

    changeIconButton->setToolTip(ui->actionChangeInstIcon->toolTip());
    renameButton->setToolTip(ui->actionRenameInstance->toolTip());

    // replace the %1 with the launcher display name in some actions
    if (helpMenuButton->toolTip().contains("%1"))
        helpMenuButton->setToolTip(helpMenuButton->toolTip().arg(BuildConfig.LAUNCHER_DISPLAYNAME));

    for (auto action : ui->helpMenu->actions()) {
        if (action->text().contains("%1"))
            action->setText(action->text().arg(BuildConfig.LAUNCHER_DISPLAYNAME));
        if (action->toolTip().contains("%1"))
            action->setToolTip(action->toolTip().arg(BuildConfig.LAUNCHER_DISPLAYNAME));
    }
}

MainWindow::~MainWindow() {}

QMenu* MainWindow::createPopupMenu()
{
    QMenu* filteredMenu = QMainWindow::createPopupMenu();
    filteredMenu->removeAction(ui->mainToolBar->toggleViewAction());

    filteredMenu->addAction(ui->actionToggleStatusBar);
    filteredMenu->addAction(ui->actionLockToolbars);

    return filteredMenu;
}
void MainWindow::setStatusBarVisibility(bool state)
{
    statusBar()->setVisible(state);
    APPLICATION->settings()->set("StatusBarVisible", state);
}
void MainWindow::lockToolbars(bool state)
{
    ui->mainToolBar->setMovable(!state);
    ui->instanceToolBar->setMovable(!state);
    ui->newsToolBar->setMovable(!state);
    APPLICATION->settings()->set("ToolbarsLocked", state);
}

void MainWindow::konamiTriggered()
{
    QString gradient =
        " stop:0 rgba(125, 0, 0, 255), stop:0.166 rgba(125, 125, 0, 255), stop:0.333 rgba(0, 125, 0, 255), stop:0.5 rgba(0, 125, 125, "
        "255), stop:0.666 rgba(0, 0, 125, 255), stop:0.833 rgba(125, 0, 125, 255), stop:1 rgba(125, 0, 0, 255));";
    QString stylesheet = "background-color: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:0," + gradient;
    if (ui->mainToolBar->styleSheet() == stylesheet) {
        ui->mainToolBar->setStyleSheet("");
        ui->instanceToolBar->setStyleSheet("");
        ui->centralWidget->setStyleSheet("");
        ui->newsToolBar->setStyleSheet("");
        ui->statusBar->setStyleSheet("");
        qDebug() << "Super Secret Mode DEACTIVATED!";
    } else {
        ui->mainToolBar->setStyleSheet(stylesheet);
        ui->instanceToolBar->setStyleSheet("background-color: qlineargradient(spread:pad, x1:0, y1:0, x2:0, y2:1," + gradient);
        ui->centralWidget->setStyleSheet("background-color: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1," + gradient);
        ui->newsToolBar->setStyleSheet(stylesheet);
        ui->statusBar->setStyleSheet(stylesheet);
        qDebug() << "Super Secret Mode ACTIVATED!";
    }
}

void MainWindow::showInstanceContextMenu(const QPoint& pos)
{
    QList<QAction*> actions;

    QAction* actionSep = new QAction("", this);
    actionSep->setSeparator(true);

    bool onInstance = view->indexAt(pos).isValid();
    if (onInstance) {
        // reuse the file menu actions
        actions = ui->fileMenu->actions();

        // remove the add instance action, launcher settings action and close action
        actions.removeFirst();
        actions.removeLast();
        actions.removeLast();

        actions.prepend(ui->actionChangeInstIcon);
        actions.prepend(ui->actionRenameInstance);

        // add header
        actions.prepend(actionSep);
        QAction* actionVoid = new QAction(m_selectedInstance->name(), this);
        actionVoid->setEnabled(false);
        actions.prepend(actionVoid);
    } else {
        auto group = view->groupNameAt(pos);

        QAction* actionVoid = new QAction(group.isNull() ? BuildConfig.LAUNCHER_DISPLAYNAME : group, this);
        actionVoid->setEnabled(false);

        QAction* actionCreateInstance = new QAction(tr("&Create instance"), this);
        actionCreateInstance->setToolTip(ui->actionAddInstance->toolTip());
        if (!group.isNull()) {
            QVariantMap instance_action_data;
            instance_action_data["group"] = group;
            actionCreateInstance->setData(instance_action_data);
        }

        connect(actionCreateInstance, &QAction::triggered, this, &MainWindow::on_actionAddInstance_triggered);

        actions.prepend(actionSep);
        actions.prepend(actionVoid);
        actions.append(actionCreateInstance);
        if (!group.isNull()) {
            QAction* actionDeleteGroup = new QAction(tr("&Delete group"), this);
            connect(actionDeleteGroup, &QAction::triggered, this, [this, group] { deleteGroup(group); });
            actions.append(actionDeleteGroup);

            QAction* actionRenameGroup = new QAction(tr("&Rename group"), this);
            connect(actionRenameGroup, &QAction::triggered, this, [this, group] { renameGroup(group); });
            actions.append(actionRenameGroup);
        }
    }
    QMenu myMenu;
    myMenu.addActions(actions);
    /*
    if (onInstance)
        myMenu.setEnabled(m_selectedInstance->canLaunch());
    */
    myMenu.exec(view->mapToGlobal(pos));
}

void MainWindow::updateMainToolBar()
{
    ui->menuBar->setVisible(APPLICATION->settings()->get("MenuBarInsteadOfToolBar").toBool());
    ui->mainToolBar->setVisible(ui->menuBar->isNativeMenuBar() || !APPLICATION->settings()->get("MenuBarInsteadOfToolBar").toBool());
}

void MainWindow::applyBigPictureMode()
{
    bool bigPicture = APPLICATION->settings()->get("BigPictureMode").toBool();

    if (bigPicture) {
        showFullScreen();  // true console feel — no taskbar, no title bar
        // Hide all chrome — only the instance grid should show
        ui->mainToolBar->hide();
        ui->instanceToolBar->hide();
        ui->newsToolBar->hide();
        ui->menuBar->hide();

        // Palette-based styling so the main screen matches the settings overlay
        const QPalette& pal = QApplication::palette();
        const QColor base = pal.color(QPalette::Base);
        const QColor mid = pal.color(QPalette::Mid);
        const QColor text = pal.color(QPalette::WindowText);

        ui->statusBar->setStyleSheet(
            QString("QStatusBar { background: %1; border-top: 1px solid %2; color: %3; }")
                .arg(base.darker(115).name(), mid.name(), text.name()));
        ui->statusBar->show();
        // Replace status bar content with controller hint bar
        m_statusLeft->hide();
        m_statusCenter->hide();
        if (!m_bpHudLabel) {
            m_bpHudLabel = new QLabel(this);
            m_bpHudLabel->setTextFormat(Qt::RichText);
            m_bpHudLabel->setAlignment(Qt::AlignCenter);
            ui->statusBar->addWidget(m_bpHudLabel, 1);
        }
        m_bpHudLabel->setStyleSheet(
            QString("QLabel { color: %1; font-size: 13px; background: transparent; }").arg(text.name()));
        updateBPHud();

        // Title header — same design language as the overlay's title bar
        if (!m_bpHeader) {
            m_bpHeader = new QLabel(this);
            QFont f = m_bpHeader->font();
            f.setPixelSize(18);
            f.setBold(true);
            m_bpHeader->setFont(f);
            m_bpHeader->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        }
        m_bpHeader->setText(tr("Prism Launcher  ›  Instances"));
        m_bpHeader->setStyleSheet(
            QString("QLabel { background: %1; color: %2; border-bottom: 1px solid %3; padding-left: 20px; }")
                .arg(base.name(), text.name(), mid.name()));
        // Push the instance grid below the header. QMainWindow ignores its own
        // contentsMargins for layout purposes, so the margin has to go on the
        // central widget's layout — otherwise the header covers the first group label.
        if (!m_bpSavedCentralMarginsValid) {
            m_bpSavedCentralMargins = ui->horizontalLayout->contentsMargins();
            m_bpSavedCentralMarginsValid = true;
        }
        QMargins bpMargins = m_bpSavedCentralMargins;
        bpMargins.setTop(bpMargins.top() + BP_HEADER_H);
        ui->horizontalLayout->setContentsMargins(bpMargins);
        m_bpHeader->setGeometry(0, 0, width(), BP_HEADER_H);
        m_bpHeader->show();
        m_bpHeader->raise();
    } else {
        if (isFullScreen())
            showMaximized();
        bpSetCursorHidden(false);
        // Restore toolbar visibility
        ui->mainToolBar->show();
        ui->instanceToolBar->show();
        ui->newsToolBar->show();
        updateMainToolBar();   // restores menuBar vs toolbar setting
        ui->statusBar->setStyleSheet(QString());
        // Restore status bar labels
        if (m_bpHudLabel) {
            ui->statusBar->removeWidget(m_bpHudLabel);
            delete m_bpHudLabel;
            m_bpHudLabel = nullptr;
        }
        m_statusLeft->show();
        m_statusCenter->show();
        if (m_bpHeader) {
            delete m_bpHeader;
            m_bpHeader = nullptr;
        }
        if (m_bpSavedCentralMarginsValid) {
            ui->horizontalLayout->setContentsMargins(m_bpSavedCentralMargins);
            m_bpSavedCentralMarginsValid = false;
        }
    }

    if (m_listDelegate)
        m_listDelegate->setBigPictureMode(bigPicture);
    view->setSpacing(bigPicture ? 12 : 5);
    view->setItemWidth(bigPicture ? ListViewDelegate::BP_ITEM_WIDTH : 100);
    view->viewport()->update();

    // Gamepad: create when entering Big Picture, destroy when leaving
    if (bigPicture && !m_gamepad) {
        m_gamepad = new GamepadController(this);

        // All navigation goes through routing slots that respect overlay state.
        // Queued on purpose: several slots open dialogs that exec() a nested event
        // loop. With direct connections that loop runs while GamepadController::
        // poll() is still on the stack, so its timer can't re-fire — the pad goes
        // dead inside the dialog and a started rumble never gets its expiry
        // processed (it buzzes until the dialog closes).
        connect(m_gamepad, &GamepadController::navigateLeft,         this, &MainWindow::onGamepadNavLeft,     Qt::QueuedConnection);
        connect(m_gamepad, &GamepadController::navigateRight,        this, &MainWindow::onGamepadNavRight,    Qt::QueuedConnection);
        connect(m_gamepad, &GamepadController::navigateUp,           this, &MainWindow::onGamepadNavUp,       Qt::QueuedConnection);
        connect(m_gamepad, &GamepadController::navigateDown,         this, &MainWindow::onGamepadNavDown,     Qt::QueuedConnection);
        connect(m_gamepad, &GamepadController::confirmPressed,       this, &MainWindow::onGamepadConfirm,     Qt::QueuedConnection);
        connect(m_gamepad, &GamepadController::cancelPressed,        this, &MainWindow::onGamepadCancel,      Qt::QueuedConnection);
        connect(m_gamepad, &GamepadController::optionsPressed,       this, &MainWindow::bpShowOptionsMenu,    Qt::QueuedConnection);
        connect(m_gamepad, &GamepadController::infoPressed,          this, &MainWindow::onGamepadInfo,        Qt::QueuedConnection);
        connect(m_gamepad, &GamepadController::shoulderLeftPressed,  this, &MainWindow::bpPrevGroup,          Qt::QueuedConnection);
        connect(m_gamepad, &GamepadController::shoulderRightPressed, this, &MainWindow::bpNextGroup,          Qt::QueuedConnection);
        connect(m_gamepad, &GamepadController::startPressed,         this, &MainWindow::onGamepadStart,       Qt::QueuedConnection);
        connect(m_gamepad, &GamepadController::triggerLeftPressed,   this, &MainWindow::onGamepadTriggerLeft, Qt::QueuedConnection);
        connect(m_gamepad, &GamepadController::triggerRightPressed,  this, &MainWindow::onGamepadTriggerRight,Qt::QueuedConnection);
        connect(m_gamepad, &GamepadController::guidePressed,         this, &MainWindow::onGamepadGuide,       Qt::QueuedConnection);
        connect(m_gamepad, &GamepadController::connectionChanged,    this, &MainWindow::onGamepadConnectionChanged);
        // Console UIs don't show a mouse cursor while a pad is driving
        connect(m_gamepad, &GamepadController::activity, this, [this] { bpSetCursorHidden(true); });
        onGamepadConnectionChanged();  // pick up a pad opened before the connects above

    } else if (!bigPicture && m_gamepad) {
        delete m_gamepad;
        m_gamepad = nullptr;
    }

    // Keyboard stand-in for the pad, for testing BP mode without hardware.
    // Gated behind PRISM_BP_KEYS=1 so it never interferes with normal use.
    // Chords avoid every key widgets consume (arrows, Enter, letters).
    if (bigPicture && !m_bpKeyShortcuts && qEnvironmentVariableIsSet("PRISM_BP_KEYS")) {
        m_bpKeyShortcuts = new QObject(this);
        auto bind = [this](const QKeySequence& seq, void (MainWindow::*slot)()) {
            auto* sc = new QShortcut(seq, m_bpKeyShortcuts);
            sc->setContext(Qt::ApplicationShortcut);
            // Queued for the same reason as the pad connects above.
            connect(sc, &QShortcut::activated, this, slot, Qt::QueuedConnection);
        };
        bind(QKeySequence("Ctrl+Alt+Left"),     &MainWindow::onGamepadNavLeft);
        bind(QKeySequence("Ctrl+Alt+Right"),    &MainWindow::onGamepadNavRight);
        bind(QKeySequence("Ctrl+Alt+Up"),       &MainWindow::onGamepadNavUp);
        bind(QKeySequence("Ctrl+Alt+Down"),     &MainWindow::onGamepadNavDown);
        bind(QKeySequence("Ctrl+Alt+Return"),   &MainWindow::onGamepadConfirm);      // A
        bind(QKeySequence("Ctrl+Alt+Backspace"),&MainWindow::onGamepadCancel);       // B
        bind(QKeySequence("Ctrl+Alt+X"),        &MainWindow::bpShowOptionsMenu);     // X
        bind(QKeySequence("Ctrl+Alt+Y"),        &MainWindow::onGamepadInfo);         // Y
        bind(QKeySequence("Ctrl+Alt+["),        &MainWindow::bpPrevGroup);           // LB
        bind(QKeySequence("Ctrl+Alt+]"),        &MainWindow::bpNextGroup);           // RB
        bind(QKeySequence("Ctrl+Alt+S"),        &MainWindow::onGamepadStart);        // Start
        bind(QKeySequence("Ctrl+Alt+PgUp"),     &MainWindow::onGamepadTriggerLeft);  // LT
        bind(QKeySequence("Ctrl+Alt+PgDown"),   &MainWindow::onGamepadTriggerRight); // RT
        bind(QKeySequence("Ctrl+Alt+G"),        &MainWindow::onGamepadGuide);        // Guide
    } else if (!bigPicture && m_bpKeyShortcuts) {
        delete m_bpKeyShortcuts;
        m_bpKeyShortcuts = nullptr;
    }

    // Options overlay panel (X button action menu): create once, reuse
    if (bigPicture && !m_bpOptionsPanel) {
        m_bpOptionsPanel = new BPOptionsMenu(this);
        connect(m_bpOptionsPanel, &BPOptionsMenu::actionSelected, this, &MainWindow::onBPOptionsAction);
    } else if (!bigPicture && m_bpOptionsPanel) {
        delete m_bpOptionsPanel;
        m_bpOptionsPanel = nullptr;
    }

    // Inline settings overlay (Y button): create once, reuse
    if (bigPicture && !m_bpSettingsOverlay) {
        m_bpSettingsOverlay = new BPSettingsOverlay(this);
        // Building the settings pages takes a couple of seconds the first time.
        // Warm the currently-selected instance once, shortly after entering BP
        // mode, so the first [Y] press opens instantly instead of freezing on
        // that build. Deliberately NOT re-run on every selection change — the
        // build is synchronous, so doing it on each grid pause would stutter
        // browsing for users who never open settings. The rarer "open settings
        // for a different instance" case falls back to building on open().
        m_bpPrewarmTimer = new QTimer(this);
        m_bpPrewarmTimer->setSingleShot(true);
        m_bpPrewarmTimer->setInterval(500);
        connect(m_bpPrewarmTimer, &QTimer::timeout, this, [this] {
            if (m_bpSettingsOverlay && !m_bpSettingsOverlay->isVisible() && m_selectedInstance)
                m_bpSettingsOverlay->prewarm(m_selectedInstance);
        });
        if (m_selectedInstance)
            m_bpPrewarmTimer->start();
    } else if (!bigPicture && m_bpSettingsOverlay) {
        m_bpSettingsOverlay->closeOverlay();
        delete m_bpSettingsOverlay;
        m_bpSettingsOverlay = nullptr;
        delete m_bpPrewarmTimer;
        m_bpPrewarmTimer = nullptr;
    }

    // In-window dialog host: Big Picture must not spawn extra windows, so every
    // QDialog shown while it's active (resource downloader, version select,
    // progress/message boxes…) gets reparented into this host. The app-wide
    // event filter below catches their Show events.
    if (bigPicture && !m_bpDialogHost) {
        m_bpDialogHost = new BPDialogHost(this);
        qApp->installEventFilter(this);
    } else if (!bigPicture && m_bpDialogHost) {
        qApp->removeEventFilter(this);
        delete m_bpDialogHost;
        m_bpDialogHost = nullptr;
    }

    // Controller-native mod download manager; pages find it via activeInstance()
    // and open it instead of the desktop ResourceDownloadDialog.
    if (bigPicture && !m_bpResourceBrowser) {
        m_bpResourceBrowser = new BPResourceBrowser(this);
        BPResourceBrowser::setActiveInstance(m_bpResourceBrowser);
    } else if (!bigPicture && m_bpResourceBrowser) {
        BPResourceBrowser::setActiveInstance(nullptr);
        delete m_bpResourceBrowser;
        m_bpResourceBrowser = nullptr;
    }

    // On-screen keyboard; text-entry surfaces find it via activeInstance().
    // While it's visible, all gamepad input routes to it (see the routing slots).
    if (bigPicture && !m_bpKeyboard) {
        m_bpKeyboard = new BPVirtualKeyboard(this);
        BPVirtualKeyboard::setActiveInstance(m_bpKeyboard);
    } else if (!bigPicture && m_bpKeyboard) {
        BPVirtualKeyboard::setActiveInstance(nullptr);
        delete m_bpKeyboard;
        m_bpKeyboard = nullptr;
    }
}

void MainWindow::bpSetCursorHidden(bool hidden)
{
    if (hidden == m_bpCursorHidden)
        return;
    m_bpCursorHidden = hidden;
    if (hidden)
        QApplication::setOverrideCursor(Qt::BlankCursor);
    else
        QApplication::restoreOverrideCursor();
}

void MainWindow::onGamepadConnectionChanged()
{
    if (!m_gamepad)
        return;
    switch (m_gamepad->padKind()) {
        case GamepadController::PadKind::PlayStation:
            bpGlyphStyle() = BPGlyphStyle::PlayStation;
            break;
        case GamepadController::PadKind::Nintendo:
            bpGlyphStyle() = BPGlyphStyle::Nintendo;
            break;
        default:
            bpGlyphStyle() = BPGlyphStyle::Xbox;
            break;
    }
    updateBPHud();
}

void MainWindow::updateBPHud()
{
    if (!m_bpHudLabel) return;
    m_bpHudLabel->setText(bpHudHtml(
        tr("[↑↓←→] Navigate    [A] Launch    [X] Options    [Y] Settings    [Start] Add    [LB/RB] Switch Group")));
}

QStringList MainWindow::bpGroupList() const
{
    // Build ordered group list in the order the proxy model presents them
    QStringList groups;
    const int n = proxymodel->rowCount();
    for (int i = 0; i < n; ++i) {
        QString g = proxymodel->index(i, 0).data(InstanceViewRoles::GroupRole).toString();
        if (!groups.contains(g))
            groups << g;
    }
    return groups;
}

void MainWindow::bpJumpToGroup(const QString& groupName)
{
    const int n = proxymodel->rowCount();
    for (int i = 0; i < n; ++i) {
        QModelIndex idx = proxymodel->index(i, 0);
        if (idx.data(InstanceViewRoles::GroupRole).toString() == groupName) {
            view->setCurrentIndex(idx);
            view->scrollTo(idx);
            return;
        }
    }
}

void MainWindow::bpPrevGroup()
{
    if (m_bpKeyboard && m_bpKeyboard->isVisible()) { m_bpKeyboard->cursorLeft(); return; }
    if (routeGamepadToDialog(Qt::Key_PageUp)) return;
    if (m_bpResourceBrowser && m_bpResourceBrowser->isVisible()) {
        m_bpResourceBrowser->pageUp();
        return;
    }
    if (m_bpOptionsPanel && m_bpOptionsPanel->isVisible()) return;
    if (m_bpSettingsOverlay && m_bpSettingsOverlay->isVisible()) {
        m_bpSettingsOverlay->tabLeft();
        return;
    }
    QStringList groups = bpGroupList();
    if (groups.isEmpty()) return;
    QModelIndex cur = view->currentIndex();
    QString curGroup = cur.isValid() ? cur.data(InstanceViewRoles::GroupRole).toString() : QString();
    int idx = groups.indexOf(curGroup);
    bpJumpToGroup(groups[(idx - 1 + groups.size()) % groups.size()]);
}

void MainWindow::bpNextGroup()
{
    if (m_bpKeyboard && m_bpKeyboard->isVisible()) { m_bpKeyboard->cursorRight(); return; }
    if (routeGamepadToDialog(Qt::Key_PageDown)) return;
    if (m_bpResourceBrowser && m_bpResourceBrowser->isVisible()) {
        m_bpResourceBrowser->pageDown();
        return;
    }
    if (m_bpOptionsPanel && m_bpOptionsPanel->isVisible()) return;
    if (m_bpSettingsOverlay && m_bpSettingsOverlay->isVisible()) {
        m_bpSettingsOverlay->tabRight();
        return;
    }
    QStringList groups = bpGroupList();
    if (groups.isEmpty()) return;
    QModelIndex cur = view->currentIndex();
    QString curGroup = cur.isValid() ? cur.data(InstanceViewRoles::GroupRole).toString() : QString();
    int idx = groups.indexOf(curGroup);
    bpJumpToGroup(groups[(idx + 1) % groups.size()]);
}

void MainWindow::bpShowOptionsMenu()
{
    if (m_bpKeyboard && m_bpKeyboard->isVisible()) { m_bpKeyboard->backspace(); return; }
    if (routeGamepadToDialog(Qt::Key_Tab)) return;  // X — focus next field in dialogs
    if (m_bpResourceBrowser && m_bpResourceBrowser->isVisible()) {
        m_bpResourceBrowser->focusSearch();
        return;
    }
    if (!m_bpOptionsPanel || !m_selectedInstance) return;
    if (m_bpOptionsPanel->isVisible()) return;
    if (m_bpSettingsOverlay && m_bpSettingsOverlay->isVisible()) {
        // X in content mode opens the action popup with all available page actions
        if (m_bpSettingsOverlay->mode() == BPSettingsOverlay::Mode::Content)
            m_bpSettingsOverlay->showActionMenu();
        return;
    }

    m_bpOptionsPanel->setInstanceName(m_selectedInstance->name());
    m_bpOptionsPanel->setGeometry(0, 0, width(), height());
    m_bpOptionsPanel->show();
    m_bpOptionsPanel->raise();
}

void MainWindow::onBPOptionsAction(BPOptionsMenu::Action action)
{
    switch (action) {
        case BPOptionsMenu::Launch:     on_actionLaunchInstance_triggered(); break;
        case BPOptionsMenu::Settings:   on_actionEditInstance_triggered();   break;
        case BPOptionsMenu::Rename:
            on_actionRenameInstance_triggered();
            // Renaming happens in the view's inline editor, which only exists
            // after the event loop spins — then give it the on-screen keyboard.
            // The delegate's editor is a QTextEdit (NoReturnTextEdit), not a
            // QLineEdit, so match any text-entry widget the keyboard supports.
            QTimer::singleShot(0, this, [this] {
                QWidget* editor = QApplication::focusWidget();
                const bool isText = qobject_cast<QLineEdit*>(editor) || qobject_cast<QTextEdit*>(editor) ||
                                    qobject_cast<QPlainTextEdit*>(editor);
                if (isText && m_bpKeyboard && view->isAncestorOf(editor))
                    m_bpKeyboard->openFor(editor);
            });
            break;
        case BPOptionsMenu::Copy:       on_actionCopyInstance_triggered();   break;
        case BPOptionsMenu::Delete:     on_actionDeleteInstance_triggered(); break;
        case BPOptionsMenu::ChangeIcon: on_actionChangeInstIcon_triggered(); break;
        default: break;
    }
}

// ── Gamepad routing slots ───────────────────────────────────────────────────

// While a dialog is open — either a real modal window or one hosted in-window by
// BPDialogHost — the gamepad drives that dialog: the button's key is posted to the
// widget that actually has focus inside it (falling back to the dialog's preferred
// focus child, which we then focus so Tab traversal starts from the right place).
// Returns false when no dialog is active.
bool MainWindow::routeGamepadToDialog(Qt::Key key)
{
    QWidget* dialog = QApplication::activeModalWidget();
    if (!dialog && m_bpDialogHost && m_bpDialogHost->isVisible())
        dialog = m_bpDialogHost->activeDialog();
    if (!dialog)
        return false;

    QWidget* fw = QApplication::focusWidget();
    QWidget* target = (fw && dialog->isAncestorOf(fw)) ? fw : BPDialogHost::preferredFocusChild(dialog);
    if (!target)
        target = dialog;
    if (target != fw)
        target->setFocus(Qt::OtherFocusReason);

    bpPostKey(target, key);
    return true;
}

void MainWindow::onGamepadNavLeft()
{
    if (m_bpKeyboard && m_bpKeyboard->isVisible()) { m_bpKeyboard->navLeft(); return; }
    if (routeGamepadToDialog(Qt::Key_Left)) return;
    if (m_bpResourceBrowser && m_bpResourceBrowser->isVisible()) return;
    if (m_bpOptionsPanel && m_bpOptionsPanel->isVisible()) return;
    if (m_bpSettingsOverlay && m_bpSettingsOverlay->isVisible()) {
        m_bpSettingsOverlay->navLeft();
        return;
    }
    bpPostKey(view, Qt::Key_Left);
}

void MainWindow::onGamepadNavRight()
{
    if (m_bpKeyboard && m_bpKeyboard->isVisible()) { m_bpKeyboard->navRight(); return; }
    if (routeGamepadToDialog(Qt::Key_Right)) return;
    if (m_bpResourceBrowser && m_bpResourceBrowser->isVisible()) return;
    if (m_bpOptionsPanel && m_bpOptionsPanel->isVisible()) return;
    if (m_bpSettingsOverlay && m_bpSettingsOverlay->isVisible()) {
        m_bpSettingsOverlay->navRight();
        return;
    }
    bpPostKey(view, Qt::Key_Right);
}

void MainWindow::onGamepadNavUp()
{
    if (m_bpKeyboard && m_bpKeyboard->isVisible()) { m_bpKeyboard->navUp(); return; }
    if (m_bpDialogHost && m_bpDialogHost->isVisible() && !QApplication::activeModalWidget()) { m_bpDialogHost->navUp(); return; }
    if (routeGamepadToDialog(Qt::Key_Up)) return;
    if (m_bpResourceBrowser && m_bpResourceBrowser->isVisible()) {
        m_bpResourceBrowser->navUp();
        return;
    }
    if (m_bpOptionsPanel && m_bpOptionsPanel->isVisible()) {
        m_bpOptionsPanel->navigatePrev();
        return;
    }
    if (m_bpSettingsOverlay && m_bpSettingsOverlay->isVisible()) {
        m_bpSettingsOverlay->navUp();
        return;
    }
    bpPostKey(view, Qt::Key_Up);
}

void MainWindow::onGamepadNavDown()
{
    if (m_bpKeyboard && m_bpKeyboard->isVisible()) { m_bpKeyboard->navDown(); return; }
    if (m_bpDialogHost && m_bpDialogHost->isVisible() && !QApplication::activeModalWidget()) { m_bpDialogHost->navDown(); return; }
    if (routeGamepadToDialog(Qt::Key_Down)) return;
    if (m_bpResourceBrowser && m_bpResourceBrowser->isVisible()) {
        m_bpResourceBrowser->navDown();
        return;
    }
    if (m_bpOptionsPanel && m_bpOptionsPanel->isVisible()) {
        m_bpOptionsPanel->navigateNext();
        return;
    }
    if (m_bpSettingsOverlay && m_bpSettingsOverlay->isVisible()) {
        m_bpSettingsOverlay->navDown();
        return;
    }
    bpPostKey(view, Qt::Key_Down);
}

void MainWindow::onGamepadConfirm()
{
    if (m_bpKeyboard && m_bpKeyboard->isVisible()) { m_bpKeyboard->pressKey(); return; }
    if (m_bpDialogHost && m_bpDialogHost->isVisible() && !QApplication::activeModalWidget()) { m_bpDialogHost->confirm(); return; }
    if (routeGamepadToDialog(Qt::Key_Return)) return;
    if (m_bpResourceBrowser && m_bpResourceBrowser->isVisible()) {
        m_bpResourceBrowser->doConfirm();
        return;
    }
    if (m_bpOptionsPanel && m_bpOptionsPanel->isVisible()) {
        m_bpOptionsPanel->confirmCurrent();
        return;
    }
    if (m_bpSettingsOverlay && m_bpSettingsOverlay->isVisible()) {
        switch (m_bpSettingsOverlay->mode()) {
            case BPSettingsOverlay::Mode::ActionMenu:
                m_bpSettingsOverlay->confirmActionMenu(); break;
            case BPSettingsOverlay::Mode::PromptCard:
                m_bpSettingsOverlay->confirmPrompt();     break;
            case BPSettingsOverlay::Mode::TabBar:
                m_bpSettingsOverlay->enterContent();      break;
            case BPSettingsOverlay::Mode::Content:
                m_bpSettingsOverlay->doConfirm();         break;
        }
        return;
    }
    if (m_gamepad)
        m_gamepad->rumble(0x8000, 0x8000, 120);  // strong pulse: the game is launching
    on_actionLaunchInstance_triggered();
}

void MainWindow::onGamepadCancel()
{
    if (m_bpKeyboard && m_bpKeyboard->isVisible()) { m_bpKeyboard->cancel(); return; }
    if (m_bpDialogHost && m_bpDialogHost->isVisible() && !QApplication::activeModalWidget()) { m_bpDialogHost->cancel(); return; }
    if (routeGamepadToDialog(Qt::Key_Escape)) return;
    if (m_bpResourceBrowser && m_bpResourceBrowser->isVisible()) {
        m_bpResourceBrowser->doCancel();
        return;
    }
    if (m_bpOptionsPanel && m_bpOptionsPanel->isVisible()) {
        m_bpOptionsPanel->dismiss();
        return;
    }
    if (m_bpSettingsOverlay && m_bpSettingsOverlay->isVisible()) {
        switch (m_bpSettingsOverlay->mode()) {
            case BPSettingsOverlay::Mode::ActionMenu:
                m_bpSettingsOverlay->dismissActionMenu(); break;
            case BPSettingsOverlay::Mode::PromptCard:
                m_bpSettingsOverlay->cancelPrompt();      break;
            case BPSettingsOverlay::Mode::Content:
                m_bpSettingsOverlay->exitToTabBar();      break;
            case BPSettingsOverlay::Mode::TabBar:
                m_bpSettingsOverlay->closeOverlay();      break;
        }
        return;
    }
}

void MainWindow::onGamepadInfo()
{
    if (m_bpKeyboard && m_bpKeyboard->isVisible()) { m_bpKeyboard->space(); return; }
    if (routeGamepadToDialog(Qt::Key_Backtab)) return;  // Y — focus previous field in dialogs
    if (m_bpResourceBrowser && m_bpResourceBrowser->isVisible()) {
        m_bpResourceBrowser->toggleProvider();
        return;
    }
    if (m_bpOptionsPanel && m_bpOptionsPanel->isVisible()) return;
    if (m_bpSettingsOverlay && m_bpSettingsOverlay->isVisible()) {
        if (m_bpSettingsOverlay->mode() == BPSettingsOverlay::Mode::ActionMenu) return;
        if (m_bpSettingsOverlay->mode() == BPSettingsOverlay::Mode::Content)
            m_bpSettingsOverlay->triggerPrimaryAction();
        else
            m_bpSettingsOverlay->enterContent();
        return;
    }
    on_actionEditInstance_triggered();
}

void MainWindow::onGamepadStart()
{
    if (m_bpKeyboard && m_bpKeyboard->isVisible()) { m_bpKeyboard->commit(); return; }
    if (routeGamepadToDialog(Qt::Key_Space)) return;  // Start — toggle/click in dialogs
    if (m_bpResourceBrowser && m_bpResourceBrowser->isVisible()) return;
    if (m_bpOptionsPanel && m_bpOptionsPanel->isVisible()) return;
    if (m_bpSettingsOverlay && m_bpSettingsOverlay->isVisible()) {
        if (m_bpSettingsOverlay->mode() == BPSettingsOverlay::Mode::Content)
            m_bpSettingsOverlay->triggerToggleAction();
        return;
    }
    // On the instance grid, Start creates a new instance. NewInstanceDialog is a
    // QDialog, so BPDialogHost picks it up and the pad can drive it. Works even
    // with no instance selected (unlike the per-instance options menu).
    on_actionAddInstance_triggered();
}

void MainWindow::onGamepadTriggerLeft()
{
    if (m_bpKeyboard && m_bpKeyboard->isVisible()) return;
    if (routeGamepadToDialog(Qt::Key_PageUp)) return;
    if (m_bpResourceBrowser && m_bpResourceBrowser->isVisible()) {
        m_bpResourceBrowser->pageUp();
        return;
    }
    if (m_bpOptionsPanel && m_bpOptionsPanel->isVisible()) return;
    if (m_bpSettingsOverlay && m_bpSettingsOverlay->isVisible()) {
        if (m_bpSettingsOverlay->mode() == BPSettingsOverlay::Mode::Content)
            m_bpSettingsOverlay->pageScroll(true);
        return;
    }
    bpPostKey(view, Qt::Key_PageUp);
}

void MainWindow::onGamepadTriggerRight()
{
    if (m_bpKeyboard && m_bpKeyboard->isVisible()) return;
    if (routeGamepadToDialog(Qt::Key_PageDown)) return;
    if (m_bpResourceBrowser && m_bpResourceBrowser->isVisible()) {
        m_bpResourceBrowser->pageDown();
        return;
    }
    if (m_bpOptionsPanel && m_bpOptionsPanel->isVisible()) return;
    if (m_bpSettingsOverlay && m_bpSettingsOverlay->isVisible()) {
        if (m_bpSettingsOverlay->mode() == BPSettingsOverlay::Mode::Content)
            m_bpSettingsOverlay->pageScroll(false);
        return;
    }
    bpPostKey(view, Qt::Key_PageDown);
}

// Guide/Home is the "take me home" button: unwind whatever is open, one layer at
// a time isn't console-like — close everything back to the instance grid.
void MainWindow::onGamepadGuide()
{
    if (m_bpKeyboard && m_bpKeyboard->isVisible())
        m_bpKeyboard->cancel();
    if (QApplication::activeModalWidget() || (m_bpDialogHost && m_bpDialogHost->isVisible())) {
        routeGamepadToDialog(Qt::Key_Escape);
        return;  // dialogs may guard unsaved work — close only the dialog layer
    }
    if (m_bpResourceBrowser && m_bpResourceBrowser->isVisible())
        m_bpResourceBrowser->closeBrowser();
    if (m_bpOptionsPanel && m_bpOptionsPanel->isVisible())
        m_bpOptionsPanel->dismiss();
    if (m_bpSettingsOverlay && m_bpSettingsOverlay->isVisible())
        m_bpSettingsOverlay->closeOverlay();
    view->setFocus();
}

void MainWindow::updateLaunchButton()
{
    QMenu* launchMenu = ui->actionLaunchInstance->menu();
    if (launchMenu)
        launchMenu->clear();
    else
        launchMenu = new QMenu(this);
    if (m_selectedInstance)
        m_selectedInstance->populateLaunchMenu(launchMenu);
    ui->actionLaunchInstance->setMenu(launchMenu);
}

void MainWindow::updateThemeMenu()
{
    QMenu* themeMenu = ui->actionChangeTheme->menu();

    if (themeMenu) {
        themeMenu->clear();
    } else {
        themeMenu = new QMenu(this);
    }

    auto themes = APPLICATION->themeManager()->getValidApplicationThemes();

    QActionGroup* themesGroup = new QActionGroup(this);

    for (auto* theme : themes) {
        QAction* themeAction = themeMenu->addAction(theme->name());

        themeAction->setCheckable(true);
        if (APPLICATION->settings()->get("ApplicationTheme").toString() == theme->id()) {
            themeAction->setChecked(true);
        }
        themeAction->setActionGroup(themesGroup);

        connect(themeAction, &QAction::triggered, [theme]() {
            APPLICATION->themeManager()->setApplicationTheme(theme->id());
            APPLICATION->settings()->set("ApplicationTheme", theme->id());
        });
    }

    ui->actionChangeTheme->setMenu(themeMenu);
}

void MainWindow::repopulateAccountsMenu()
{
    ui->accountsMenu->clear();

    // NOTE: this is done so the accounts button text is not set to the accounts menu title
    QMenu* accountsButtonMenu = ui->actionAccountsButton->menu();
    if (accountsButtonMenu) {
        accountsButtonMenu->clear();
    } else {
        accountsButtonMenu = new QMenu(this);
        ui->actionAccountsButton->setMenu(accountsButtonMenu);
    }

    auto accounts = APPLICATION->accounts();
    MinecraftAccountPtr defaultAccount = accounts->defaultAccount();
    
    bool canChangeSkin = defaultAccount && (defaultAccount->accountType() == AccountType::MSA) && !defaultAccount->isActive();
    ui->actionManageSkins->setEnabled(canChangeSkin);

    QString active_profileId = "";
    if (defaultAccount) {
        // this can be called before accountMenuButton exists
        if (ui->actionAccountsButton) {
            auto profileLabel = profileInUseFilter(defaultAccount->displayName(), defaultAccount->isInUse());
            ui->actionAccountsButton->setText(profileLabel);
        }
    }

    QActionGroup* accountsGroup = new QActionGroup(this);

    if (accounts->count() <= 0) {
        ui->actionNoAccountsAdded->setEnabled(false);
        ui->accountsMenu->addAction(ui->actionNoAccountsAdded);
    } else {
        // TODO: Nicer way to iterate?
        for (int i = 0; i < accounts->count(); i++) {
            MinecraftAccountPtr account = accounts->at(i);
            auto profileLabel = profileInUseFilter(account->displayName(), account->isInUse());
            QAction* action = new QAction(profileLabel, this);
            action->setData(i);
            action->setCheckable(true);
            action->setActionGroup(accountsGroup);
            if (defaultAccount == account) {
                action->setChecked(true);
            }

            auto face = account->getFace();
            if (!face.isNull()) {
                action->setIcon(face);
            } else {
                action->setIcon(QIcon::fromTheme("noaccount"));
            }

            const int highestNumberKey = 9;
            if (i < highestNumberKey) {
                action->setShortcut(QKeySequence(tr("Ctrl+%1").arg(i + 1)));
            }

            ui->accountsMenu->addAction(action);
            connect(action, &QAction::triggered, this, &MainWindow::changeActiveAccount);
        }
    }

    ui->accountsMenu->addSeparator();

    ui->actionNoDefaultAccount->setData(-1);
    ui->actionNoDefaultAccount->setChecked(!defaultAccount);
    ui->actionNoDefaultAccount->setActionGroup(accountsGroup);

    ui->accountsMenu->addAction(ui->actionNoDefaultAccount);

    connect(ui->actionNoDefaultAccount, &QAction::triggered, this, &MainWindow::changeActiveAccount);

    ui->accountsMenu->addSeparator();
    ui->accountsMenu->addAction(ui->actionManageSkins);
    ui->accountsMenu->addAction(ui->actionManageAccounts);

    accountsButtonMenu->addActions(ui->accountsMenu->actions());
}

void MainWindow::updatesAllowedChanged(bool allowed)
{
    if (!APPLICATION->updaterEnabled()) {
        return;
    }
    ui->actionCheckUpdate->setEnabled(allowed);
}

/*
 * Assumes the sender is a QAction
 */
void MainWindow::changeActiveAccount()
{
    QAction* sAction = (QAction*)sender();

    // Profile's associated Mojang username
    if (sAction->data().typeId() != QMetaType::Int)
        return;

    QVariant action_data = sAction->data();
    bool valid = false;
    int index = action_data.toInt(&valid);
    if (!valid) {
        index = -1;
    }
    auto accounts = APPLICATION->accounts();
    accounts->setDefaultAccount(index == -1 ? nullptr : accounts->at(index));
    defaultAccountChanged();
}

void MainWindow::defaultAccountChanged()
{
    repopulateAccountsMenu();

    MinecraftAccountPtr account = APPLICATION->accounts()->defaultAccount();

    // FIXME: this needs adjustment for MSA
    if (account && account->profileName() != "") {
        auto profileLabel = profileInUseFilter(account->displayName(), account->isInUse());
        ui->actionAccountsButton->setText(profileLabel);
        auto face = account->getFace();
        if (face.isNull()) {
            ui->actionAccountsButton->setIcon(QIcon::fromTheme("noaccount"));
        } else {
            ui->actionAccountsButton->setIcon(face);
        }
        return;
    }

    // Set the icon to the "no account" icon.
    ui->actionAccountsButton->setIcon(QIcon::fromTheme("noaccount"));
    ui->actionAccountsButton->setText(tr("Accounts"));
}

bool MainWindow::eventFilter(QObject* obj, QEvent* ev)
{
    // Touching the mouse brings the cursor back (it hides while the pad drives).
    // MouseMove alone isn't enough — most widgets don't enable mouse tracking,
    // so also catch hover and click events.
    if (m_bpCursorHidden &&
        (ev->type() == QEvent::MouseMove || ev->type() == QEvent::HoverMove ||
         ev->type() == QEvent::MouseButtonPress || ev->type() == QEvent::Wheel))
        bpSetCursorHidden(false);

    // Big Picture: reparent any QDialog being shown into the in-window host so no
    // separate windows ever appear. Native dialogs (file pickers) never emit a
    // widget Show event, so they are naturally excluded.
    if (m_bpDialogHost && ev->type() == QEvent::Show) {
        if (auto* dlg = qobject_cast<QDialog*>(obj);
            dlg && dlg->isWindow() && !dlg->property("bpHosted").toBool())
            m_bpDialogHost->hostDialog(dlg);
    }

    // Route physical arrow/confirm/cancel key presses to the overlay nav slots.
    // nativeScanCode() == 0 for synthetic events (our own postEvent calls), so we
    // only intercept real hardware events and avoid re-entering the nav handlers.
    if (m_bpSettingsOverlay && m_bpSettingsOverlay->isVisible()) {
        if (ev->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(ev);
            if (ke->nativeScanCode() != 0 && ke->modifiers() == Qt::NoModifier) {
                switch (ke->key()) {
                    case Qt::Key_Up:     onGamepadNavUp();   return true;
                    case Qt::Key_Down:   onGamepadNavDown(); return true;
                    case Qt::Key_Left:   onGamepadNavLeft(); return true;
                    case Qt::Key_Right:  onGamepadNavRight();return true;
                    case Qt::Key_Return:
                    case Qt::Key_Enter:  onGamepadConfirm(); return true;
                    case Qt::Key_Escape: onGamepadCancel();  return true;
                    default: break;
                }
            }
        }
    }
    if (obj == view) {
        if (ev->type() == QEvent::KeyPress) {
            secretEventFilter->input(ev);
            QKeyEvent* keyEvent = static_cast<QKeyEvent*>(ev);
            switch (keyEvent->key()) {
                    /*
                case Qt::Key_Enter:
                case Qt::Key_Return:
                    activateInstance(m_selectedInstance);
                    return true;
                    */
                case Qt::Key_Delete:
                    on_actionDeleteInstance_triggered();
                    return true;
                case Qt::Key_F5:
                    refreshInstances();
                    return true;
                case Qt::Key_F2:
                    on_actionRenameInstance_triggered();
                    return true;
                default:
                    break;
            }
        }
    }
    return QMainWindow::eventFilter(obj, ev);
}

void MainWindow::updateNewsLabel()
{
    if (m_newsChecker->isLoadingNews()) {
        newsLabel->setText(tr("Loading news..."));
        newsLabel->setEnabled(false);
        ui->actionMoreNews->setVisible(false);
    } else {
        QList<NewsEntryPtr> entries = m_newsChecker->getNewsEntries();
        if (entries.length() > 0) {
            newsLabel->setText(entries[0]->title);
            newsLabel->setEnabled(true);
            ui->actionMoreNews->setVisible(true);
        } else {
            newsLabel->setText(tr("No news available."));
            newsLabel->setEnabled(false);
            ui->actionMoreNews->setVisible(false);
        }
    }
}

QList<int> stringToIntList(const QString& string)
{
    QStringList split = string.split(',', Qt::SkipEmptyParts);
    QList<int> out;
    for (int i = 0; i < split.size(); ++i) {
        out.append(split.at(i).toInt());
    }
    return out;
}
QString intListToString(const QList<int>& list)
{
    QStringList slist;
    for (int i = 0; i < list.size(); ++i) {
        slist.append(QString::number(list.at(i)));
    }
    return slist.join(',');
}

void MainWindow::onCatToggled(bool state)
{
    setCatBackground(state);
    APPLICATION->settings()->set("TheCat", state);
}

void MainWindow::setCatBackground(bool enabled)
{
    view->setPaintCat(enabled);
    view->viewport()->repaint();
}

void MainWindow::runModalTask(Task* task)
{
    connect(task, &Task::failed,
            [this](QString reason) { CustomMessageBox::selectable(this, tr("Error"), reason, QMessageBox::Critical)->show(); });
    connect(task, &Task::succeeded, [this, task]() {
        QStringList warnings = task->warnings();
        if (warnings.count()) {
            CustomMessageBox::selectable(this, tr("Warnings"), warnings.join('\n'), QMessageBox::Warning)->show();
        }
    });
    connect(task, &Task::aborted, [this] {
        CustomMessageBox::selectable(this, tr("Task aborted"), tr("The task has been aborted by the user."), QMessageBox::Information)
            ->show();
    });
    ProgressDialog loadDialog(this);
    loadDialog.setSkipButton(true, tr("Abort"));
    loadDialog.execWithTask(task);
}

void MainWindow::instanceFromInstanceTask(InstanceTask* rawTask)
{
    unique_qobject_ptr<Task> task(APPLICATION->instances()->wrapInstanceTask(rawTask));
    runModalTask(task.get());
}

void MainWindow::on_actionCopyInstance_triggered()
{
    if (!m_selectedInstance)
        return;

    CopyInstanceDialog copyInstDlg(m_selectedInstance, this);
    if (!copyInstDlg.exec())
        return;

    auto copyTask = new InstanceCopyTask(m_selectedInstance, copyInstDlg.getChosenOptions());
    copyTask->setName(copyInstDlg.instName());
    copyTask->setGroup(copyInstDlg.instGroup());
    copyTask->setIcon(copyInstDlg.iconKey());
    unique_qobject_ptr<Task> task(APPLICATION->instances()->wrapInstanceTask(copyTask));
    runModalTask(task.get());
}

void MainWindow::addInstance(const QString& url, const QMap<QString, QString>& extra_info)
{
    QString groupName;
    do {
        QObject* obj = sender();
        if (!obj)
            break;
        QAction* action = qobject_cast<QAction*>(obj);
        if (!action)
            break;
        auto map = action->data().toMap();
        if (!map.contains("group"))
            break;
        groupName = map["group"].toString();
    } while (0);

    if (groupName.isEmpty()) {
        groupName = APPLICATION->settings()->get("LastUsedGroupForNewInstance").toString();
    }

    NewInstanceDialog newInstDlg(groupName, url, extra_info, this);
    if (!newInstDlg.exec())
        return;

    APPLICATION->settings()->set("LastUsedGroupForNewInstance", newInstDlg.instGroup());

    InstanceTask* creationTask = newInstDlg.extractTask();
    if (creationTask) {
        instanceFromInstanceTask(creationTask);
    }
}

void MainWindow::on_actionAddInstance_triggered()
{
    addInstance();
}

void MainWindow::processURLs(QList<QUrl> urls)
{
    // NOTE: This loop only processes one dropped file!
    for (auto& url : urls) {
        if (url.isEmpty() || url.toString().trimmed().isEmpty())
            continue;

        qDebug() << "Processing" << url;

        // The isLocalFile() check below doesn't work as intended without an explicit scheme.
        if (url.scheme().isEmpty())
            url.setScheme("file");

        ModPlatform::IndexedVersion version;
        QMap<QString, QString> extra_info;
        QUrl local_url;
        if (!url.isLocalFile()) {  // download the remote resource and identify

            const bool isExternalURLImport = (url.host().toLower() == "import") || (url.path().startsWith("/import", Qt::CaseInsensitive));

            QUrl dl_url;
            if (url.scheme() == "curseforge" || (url.scheme() == BuildConfig.LAUNCHER_APP_BINARY_NAME && url.host() == "install")) {
                // need to find the download link for the modpack / resource
                // format of url curseforge://install?addonId=IDHERE&fileId=IDHERE
                // format of url binaryname://install?platform=curseforge&addonId=IDHERE&fileId=IDHERE
                QUrlQuery query(url);

                // check if this is a binaryname:// url
                if (url.scheme() == BuildConfig.LAUNCHER_APP_BINARY_NAME) {
                    // check this is an curseforge platform request
                    if (query.queryItemValue("platform").toLower() != "curseforge") {
                        qDebug() << "Invalid mod distribution platform:" << query.queryItemValue("platform");
                        continue;
                    }
                }

                if (query.allQueryItemValues("addonId").isEmpty() || query.allQueryItemValues("fileId").isEmpty()) {
                    qDebug() << "Invalid curseforge link:" << url;
                    continue;
                }

                auto addonId = query.allQueryItemValues("addonId")[0];
                auto fileId = query.allQueryItemValues("fileId")[0];

                extra_info.insert("pack_id", addonId);
                extra_info.insert("pack_version_id", fileId);

                auto api = FlameAPI();
                auto [job, array] = api.getFile(addonId, fileId);

                connect(job.get(), &Task::failed, this,
                        [this](QString reason) { CustomMessageBox::selectable(this, tr("Error"), reason, QMessageBox::Critical)->show(); });
                connect(job.get(), &Task::succeeded, this, [this, array, addonId, fileId, &dl_url, &version] {
                    qDebug() << "Returned CFURL Json:\n" << array->toStdString().c_str();
                    auto doc = Json::requireDocument(*array);
                    auto data = doc.object()["data"].toObject();
                    // No way to find out if it's a mod or a modpack before here
                    // And also we need to check if it ends with .zip, instead of any better way
                    version = FlameMod::loadIndexedPackVersion(data);
                    auto fileName = version.fileName;

                    // Have to use ensureString then use QUrl to get proper url encoding
                    dl_url = QUrl(version.downloadUrl);
                    if (!dl_url.isValid()) {
                        CustomMessageBox::selectable(
                            this, tr("Error"),
                            tr("The modpack, mod, or resource %1 is blocked for third-parties! Please download it manually.").arg(fileName),
                            QMessageBox::Critical)
                            ->show();
                        return;
                    }

                    QFileInfo dl_file(dl_url.fileName());
                });

                {  // drop stack
                    ProgressDialog dlUrlDialod(this);
                    dlUrlDialod.setSkipButton(true, tr("Abort"));
                    dlUrlDialod.execWithTask(job.get());
                }

            } else if (url.scheme() == BuildConfig.LAUNCHER_APP_BINARY_NAME && !isExternalURLImport) {
                QVariantMap receivedData;
                const QUrlQuery query(url.query());
                const auto items = query.queryItems();
                for (auto it = items.begin(), end = items.end(); it != end; ++it)
                    receivedData.insert(it->first, it->second);
                emit APPLICATION->oauthReplyRecieved(receivedData);
                continue;
            } else if ((url.scheme() == "prismlauncher" || url.scheme() == BuildConfig.LAUNCHER_APP_BINARY_NAME) && isExternalURLImport) {
                // PrismLauncher URL protocol modpack import
                // works for any prism fork
                // preferred import format: prismlauncher://import?url=ENCODED
                const auto host = url.host().toLower();
                const auto path = url.path();

                QString encodedTarget;

                {
                    QUrlQuery query(url);
                    const auto values = query.allQueryItemValues("url");
                    if (!values.isEmpty()) {
                        encodedTarget = values.first();
                    }
                }

                // alternative import format: prismlauncher://import/ENCODED
                if (encodedTarget.isEmpty()) {
                    QString p = path;

                    if (p.startsWith("/import/", Qt::CaseInsensitive)) {
                        p = p.mid(QString("/import/").size());
                    } else if (host == "import" && p.startsWith("/")) {
                        p = p.mid(1);
                    }

                    if (!p.isEmpty() && p != "/import") {
                        encodedTarget = p;
                    }
                }

                if (encodedTarget.isEmpty()) {
                    CustomMessageBox::selectable(this, tr("Error"), tr("Invalid import link: missing 'url' parameter."),
                                                 QMessageBox::Critical)
                        ->show();
                    continue;
                }

                const QString decodedStr = QUrl::fromPercentEncoding(encodedTarget.toUtf8()).trimmed();

                QUrl target = QUrl::fromUserInput(decodedStr);

                // Validate: only allow http(s)
                if (!target.isValid() || (target.scheme() != "https" && target.scheme() != "http")) {
                    CustomMessageBox::selectable(this, tr("Error"), tr("Invalid import link: URL must be http(s)."), QMessageBox::Critical)
                        ->show();
                    continue;
                }

                const auto res = QMessageBox::question(
                    this, tr("Install modpack"),
                    tr("Do you want to download and import a modpack from:\n%1\n\nURL:\n%2").arg(target.host(), target.toString()),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
                if (res != QMessageBox::Yes) {
                    continue;
                }

                dl_url = target;
            } else {
                dl_url = url;
            }

            if (!dl_url.isValid()) {
                continue;  // no valid url to download this resource
            }

            const QString path = dl_url.host() + '/' + dl_url.path();
            auto entry = APPLICATION->metacache()->resolveEntry("general", path);
            entry->setStale(true);
            auto dl_job = unique_qobject_ptr<NetJob>(new NetJob(tr("Modpack download"), APPLICATION->network()));
            dl_job->addNetAction(Net::ApiDownload::makeCached(dl_url, entry));
            auto archivePath = entry->getFullPath();

            bool dl_success = false;
            connect(dl_job.get(), &Task::failed, this,
                    [this](QString reason) { CustomMessageBox::selectable(this, tr("Error"), reason, QMessageBox::Critical)->show(); });
            connect(dl_job.get(), &Task::succeeded, this, [&dl_success] { dl_success = true; });

            {  // drop stack
                ProgressDialog dlUrlDialod(this);
                dlUrlDialod.setSkipButton(true, tr("Abort"));
                dlUrlDialod.execWithTask(dl_job.get());
            }

            if (!dl_success) {
                continue;  // no local file to identify
            }
            local_url = QUrl::fromLocalFile(archivePath);

        } else {
            local_url = url;
        }

        auto localFileName = QDir::toNativeSeparators(local_url.toLocalFile());
        QFileInfo localFileInfo(localFileName);

        if (localFileName.isEmpty() || !localFileInfo.exists()) {
            qDebug() << "Ignoring invalid path" << localFileName;
            continue;
        }

        auto type = ResourceUtils::identify(localFileInfo);

        if (ModPlatform::ResourceTypeUtils::VALID_RESOURCES.count(type) == 0) {  // probably instance/modpack
            addInstance(localFileName, extra_info);
            continue;
        }

        if (APPLICATION->instances()->count() <= 0) {
            CustomMessageBox::selectable(this, tr("No instance!"),
                                         tr("No instance available to add the resource to.\nPlease create a new instance before "
                                            "attempting to install this resource again."),
                                         QMessageBox::Critical)
                ->show();
            continue;
        }
        ImportResourceDialog dlg(localFileName, type, this);

        if (dlg.exec() != QDialog::Accepted)
            continue;

        qDebug() << "Adding resource" << localFileName << "to" << dlg.selectedInstanceKey;

        auto inst = APPLICATION->instances()->getInstanceById(dlg.selectedInstanceKey);
        auto minecraftInst = dynamic_cast<MinecraftInstance*>(inst);

        switch (type) {
            case ModPlatform::ResourceType::ResourcePack:
                minecraftInst->resourcePackList()->installResourceWithFlameMetadata(localFileName, version);
                break;
            case ModPlatform::ResourceType::TexturePack:
                minecraftInst->texturePackList()->installResourceWithFlameMetadata(localFileName, version);
                break;
            case ModPlatform::ResourceType::DataPack:
                qWarning() << "Importing of Data Packs not supported at this time. Ignoring" << localFileName;
                break;
            case ModPlatform::ResourceType::Mod:
                minecraftInst->loaderModList()->installResourceWithFlameMetadata(localFileName, version);
                break;
            case ModPlatform::ResourceType::ShaderPack:
                minecraftInst->shaderPackList()->installResourceWithFlameMetadata(localFileName, version);
                break;
            case ModPlatform::ResourceType::World:
                minecraftInst->worldList()->installWorld(localFileInfo);
                break;
            case ModPlatform::ResourceType::Unknown:
            default:
                qDebug() << "Can't Identify" << localFileName << "Ignoring it.";
                break;
        }
    }
}

void MainWindow::on_actionREDDIT_triggered()
{
    DesktopServices::openUrl(QUrl(BuildConfig.SUBREDDIT_URL));
}

void MainWindow::on_actionDISCORD_triggered()
{
    DesktopServices::openUrl(QUrl(BuildConfig.DISCORD_URL));
}

void MainWindow::on_actionMATRIX_triggered()
{
    DesktopServices::openUrl(QUrl(BuildConfig.MATRIX_URL));
}

void MainWindow::on_actionChangeInstIcon_triggered()
{
    if (!m_selectedInstance)
        return;

    IconPickerDialog dlg(this);
    dlg.execWithSelection(m_selectedInstance->iconKey());
    if (dlg.result() == QDialog::Accepted) {
        m_selectedInstance->setIconKey(dlg.selectedIconKey);
        auto icon = APPLICATION->icons()->getIcon(dlg.selectedIconKey);
        ui->actionChangeInstIcon->setIcon(icon);
        changeIconButton->setIcon(icon);
    }
}

void MainWindow::iconUpdated(QString icon)
{
    if (icon == m_currentInstIcon) {
        auto new_icon = APPLICATION->icons()->getIcon(m_currentInstIcon);
        ui->actionChangeInstIcon->setIcon(new_icon);
        changeIconButton->setIcon(new_icon);
    }
}

void MainWindow::updateInstanceToolIcon(QString new_icon)
{
    m_currentInstIcon = new_icon;
    auto icon = APPLICATION->icons()->getIcon(m_currentInstIcon);
    ui->actionChangeInstIcon->setIcon(icon);
    changeIconButton->setIcon(icon);
}

void MainWindow::setSelectedInstanceById(const QString& id)
{
    if (id.isNull())
        return;
    const QModelIndex index = APPLICATION->instances()->getInstanceIndexById(id);
    if (index.isValid()) {
        QModelIndex selectionIndex = proxymodel->mapFromSource(index);
        view->selectionModel()->setCurrentIndex(selectionIndex, QItemSelectionModel::ClearAndSelect);
        updateStatusCenter();
    }
}

void MainWindow::on_actionChangeInstGroup_triggered()
{
    if (!m_selectedInstance)
        return;

    InstanceId instId = m_selectedInstance->id();
    QString src(APPLICATION->instances()->getInstanceGroup(instId));

    QStringList groups = APPLICATION->instances()->getGroups();
    groups.prepend("");
    int index = groups.indexOf(src);
    bool ok = false;
    QString dst = QInputDialog::getItem(this, tr("Group name"), tr("Enter a new group name."), groups, index, true, &ok);
    dst = dst.simplified();

    if (ok) {
        APPLICATION->instances()->setInstanceGroup(instId, dst);
    }
}

void MainWindow::deleteGroup(QString group)
{
    Q_ASSERT(!group.isEmpty());

    const int reply = QMessageBox::question(this, tr("Delete group"), tr("Are you sure you want to delete the group '%1'?").arg(group),
                                            QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes)
        APPLICATION->instances()->deleteGroup(group);
}

void MainWindow::renameGroup(QString group)
{
    Q_ASSERT(!group.isEmpty());

    QString name = QInputDialog::getText(this, tr("Rename group"), tr("Enter a new group name."), QLineEdit::Normal, group);
    name = name.simplified();
    if (name.isNull() || name == group)
        return;

    const bool empty = name.isEmpty();
    const bool duplicate = APPLICATION->instances()->getGroups().contains(name, Qt::CaseInsensitive) && group.toLower() != name.toLower();

    if (empty || duplicate) {
        QMessageBox::warning(this, tr("Cannot rename group"), empty ? tr("Cannot set empty name.") : tr("Group already exists. :/"));
        return;
    }

    APPLICATION->instances()->renameGroup(group, name);
}

void MainWindow::undoTrashInstance()
{
    if (!APPLICATION->instances()->undoTrashInstance())
        QMessageBox::warning(
            this, tr("Failed to undo trashing instance"),
            tr("Some instances and shortcuts could not be restored.\nPlease check your trashbin to manually restore them."));
    ui->actionUndoTrashInstance->setEnabled(APPLICATION->instances()->trashedSomething());
}

void MainWindow::on_actionViewLauncherRootFolder_triggered()
{
    DesktopServices::openPath(".");
}

void MainWindow::on_actionViewInstanceFolder_triggered()
{
    QString str = APPLICATION->settings()->get("InstanceDir").toString();
    DesktopServices::openPath(str);
}

void MainWindow::on_actionViewCentralModsFolder_triggered()
{
    DesktopServices::openPath(APPLICATION->settings()->get("CentralModsDir").toString(), true);
}

void MainWindow::on_actionViewSkinsFolder_triggered()
{
    DesktopServices::openPath(APPLICATION->settings()->get("SkinsDir").toString(), true);
}

void MainWindow::on_actionViewIconThemeFolder_triggered()
{
    DesktopServices::openPath(APPLICATION->themeManager()->getIconThemesFolder().path(), true);
}

void MainWindow::on_actionViewWidgetThemeFolder_triggered()
{
    DesktopServices::openPath(APPLICATION->themeManager()->getApplicationThemesFolder().path(), true);
}

void MainWindow::on_actionViewCatPackFolder_triggered()
{
    DesktopServices::openPath(APPLICATION->themeManager()->getCatPacksFolder().path(), true);
}

void MainWindow::on_actionViewIconsFolder_triggered()
{
    DesktopServices::openPath(APPLICATION->icons()->getDirectory(), true);
}

void MainWindow::on_actionViewLogsFolder_triggered()
{
    DesktopServices::openPath("logs", true);
}

void MainWindow::on_actionViewJavaFolder_triggered()
{
    DesktopServices::openPath(APPLICATION->javaPath(), true);
}

void MainWindow::refreshInstances()
{
    APPLICATION->instances()->loadList();
}

void MainWindow::checkForUpdates()
{
    if (APPLICATION->updaterEnabled()) {
        APPLICATION->triggerUpdateCheck();
    } else {
        qWarning() << "Updater not set up. Cannot check for updates.";
    }
}

void MainWindow::on_actionSettings_triggered()
{
    APPLICATION->ShowGlobalSettings(this, "global-settings");
}

void MainWindow::globalSettingsClosed()
{
    // FIXME: quick HACK to make this work. improve, optimize.
    APPLICATION->instances()->loadList();
    proxymodel->invalidate();
    proxymodel->sort(0);
    updateMainToolBar();
    updateLaunchButton();
    updateThemeMenu();
    updateStatusCenter();
    applyBigPictureMode();
    // This needs to be done to prevent UI elements disappearing in the event the config is changed
    // but Prism Launcher exits abnormally, causing the window state to never be saved:
    APPLICATION->settings()->set("MainWindowState", QString::fromUtf8(saveState().toBase64()));
    update();
}

void MainWindow::on_actionEditInstance_triggered()
{
    if (!m_selectedInstance)
        return;

    if (!m_selectedInstance->canEdit()) {
        CustomMessageBox::selectable(this, tr("Instance not editable"),
                                     tr("This instance is not editable. It may be broken, invalid, or too old. Check logs for details."),
                                     QMessageBox::Critical)
            ->show();
        return;
    }

    if (APPLICATION->settings()->get("BigPictureMode").toBool()) {
        // In BP mode: show inline settings overlay instead of a separate window
        if (m_bpSettingsOverlay && !m_bpSettingsOverlay->isVisible())
            m_bpSettingsOverlay->open(m_selectedInstance);
    } else {
        APPLICATION->showInstanceWindow(m_selectedInstance);
    }
}

void MainWindow::on_actionManageSkins_triggered()
{
    auto account = APPLICATION->accounts()->defaultAccount();

    if (account && (account->accountType() == AccountType::MSA) && !account->isActive()) {
        SkinManageDialog dialog(this, account);
        dialog.exec();
    }
}

void MainWindow::on_actionManageAccounts_triggered()
{
    APPLICATION->ShowGlobalSettings(this, "accounts");
}

void MainWindow::on_actionReportBug_triggered()
{
    DesktopServices::openUrl(QUrl(BuildConfig.BUG_TRACKER_URL));
}

void MainWindow::on_actionClearMetadata_triggered()
{
    // This if contains side effects!
    if (!APPLICATION->metacache()->evictAll()) {
        CustomMessageBox::selectable(this, tr("Error"),
                                     tr("Metadata cache clear Failed!\nTo clear the metadata cache manually, press Folders -> View "
                                        "Launcher Root Folder, and after closing the launcher delete the folder named \"meta\"\n"),
                                     QMessageBox::Warning)
            ->show();
    }

    APPLICATION->metacache()->SaveNow();
}

#ifdef Q_OS_MAC
void MainWindow::on_actionAddToPATH_triggered()
{
    auto binaryPath = APPLICATION->applicationFilePath();
    auto targetPath = QString("/usr/local/bin/%1").arg(BuildConfig.LAUNCHER_APP_BINARY_NAME);
    qDebug() << "Symlinking" << binaryPath << "to" << targetPath;

    QStringList args;
    args << "-e";
    args << QString("do shell script \"mkdir -p /usr/local/bin && ln -sf '%1' '%2'\" with administrator privileges")
                .arg(binaryPath, targetPath);
    auto outcome = QProcess::execute("/usr/bin/osascript", args);
    if (!outcome) {
        QMessageBox::information(this, tr("Successfully added %1 to PATH").arg(BuildConfig.LAUNCHER_DISPLAYNAME),
                                 tr("%1 was successfully added to your PATH. You can now start it by running `%2`.")
                                     .arg(BuildConfig.LAUNCHER_DISPLAYNAME, BuildConfig.LAUNCHER_APP_BINARY_NAME));
    } else {
        QMessageBox::critical(this, tr("Failed to add %1 to PATH").arg(BuildConfig.LAUNCHER_DISPLAYNAME),
                              tr("An error occurred while trying to add %1 to PATH").arg(BuildConfig.LAUNCHER_DISPLAYNAME));
    }
}
#endif

void MainWindow::on_actionOpenWiki_triggered()
{
    DesktopServices::openUrl(QUrl(BuildConfig.WIKI_URL));
}

void MainWindow::on_actionMoreNews_triggered()
{
    auto entries = m_newsChecker->getNewsEntries();
    NewsDialog news_dialog(entries, this);
    news_dialog.exec();
}

void MainWindow::newsButtonClicked()
{
    auto entries = m_newsChecker->getNewsEntries();
    NewsDialog news_dialog(entries, this);
    news_dialog.toggleArticleList();
    news_dialog.exec();
}

void MainWindow::onCatChanged(int)
{
    setCatBackground(APPLICATION->settings()->get("TheCat").toBool());
}

void MainWindow::on_actionAbout_triggered()
{
    AboutDialog dialog(this);
    dialog.exec();
}

void MainWindow::on_actionDeleteInstance_triggered()
{
    if (!m_selectedInstance) {
        return;
    }

    if (m_selectedInstance->isRunning()) {
        CustomMessageBox::selectable(this, tr("Cannot Delete Running Instance"),
                                     tr("The selected instance is currently running and cannot be deleted. Please stop the instance before "
                                        "attempting to delete it."),
                                     QMessageBox::Warning, QMessageBox::Ok)
            ->exec();
        return;
    }
    auto id = m_selectedInstance->id();

    QString shortcutStr;
    auto shortcuts = m_selectedInstance->shortcuts();
    if (!shortcuts.isEmpty())
        shortcutStr = tr(" and its %n registered shortcut(s)", "", shortcuts.size());
    auto response = CustomMessageBox::selectable(this, tr("Confirm Deletion"),
                                                 tr("You are about to delete \"%1\"%2.\n"
                                                    "This may be permanent and will completely delete the instance.\n\n"
                                                    "Are you sure?")
                                                     .arg(m_selectedInstance->name(), shortcutStr),
                                                 QMessageBox::Warning, QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                        ->exec();

    if (response != QMessageBox::Yes)
        return;

    if (!checkLinkedInstances(id, this, tr("Deleting")))
        return;

    if (APPLICATION->instances()->trashInstance(id)) {
        ui->actionUndoTrashInstance->setEnabled(APPLICATION->instances()->trashedSomething());
    } else {
        APPLICATION->instances()->deleteInstance(id);
    }
    APPLICATION->settings()->set("SelectedInstance", QString());
    selectionBad();
}

void MainWindow::on_actionExportInstanceZip_triggered()
{
    if (m_selectedInstance) {
        ExportInstanceDialog dlg(m_selectedInstance, this);
        dlg.exec();
    }
}

void MainWindow::on_actionExportInstanceMrPack_triggered()
{
    if (m_selectedInstance) {
        auto instance = dynamic_cast<MinecraftInstance*>(m_selectedInstance);
        if (instance != nullptr) {
            ExportPackDialog dlg(instance, this);
            dlg.exec();
        }
    }
}

void MainWindow::on_actionExportInstanceFlamePack_triggered()
{
    if (m_selectedInstance) {
        auto instance = dynamic_cast<MinecraftInstance*>(m_selectedInstance);
        if (instance) {
            if (auto cmp = instance->getPackProfile()->getComponent("net.minecraft");
                cmp && cmp->getVersionFile() && cmp->getVersionFile()->type == "snapshot") {
                QMessageBox msgBox(this);
                msgBox.setText("Snapshots are currently not supported by CurseForge modpacks.");
                msgBox.exec();
                return;
            }
            ExportPackDialog dlg(instance, this, ModPlatform::ResourceProvider::FLAME);
            dlg.exec();
        }
    }
}

void MainWindow::on_actionRenameInstance_triggered()
{
    if (m_selectedInstance) {
        view->edit(view->currentIndex());
    }
}

void MainWindow::on_actionViewSelectedInstFolder_triggered()
{
    if (m_selectedInstance) {
        QString str = m_selectedInstance->instanceRoot();
        DesktopServices::openPath(QFileInfo(str));
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // Save the window state and geometry.
    APPLICATION->settings()->set("MainWindowState", QString::fromUtf8(saveState().toBase64()));
    APPLICATION->settings()->set("MainWindowGeometry", QString::fromUtf8(saveGeometry().toBase64()));
    instanceToolbarSetting->set(QString::fromUtf8(ui->instanceToolBar->getVisibilityState().toBase64()));
    event->accept();
    emit isClosing();
}

void MainWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    // Keep the full-window Big Picture layers glued to the window size.
    if (m_bpHeader)
        m_bpHeader->setGeometry(0, 0, width(), BP_HEADER_H);
    if (m_bpSettingsOverlay && m_bpSettingsOverlay->isVisible())
        m_bpSettingsOverlay->setGeometry(0, 0, width(), height());
    if (m_bpOptionsPanel && m_bpOptionsPanel->isVisible())
        m_bpOptionsPanel->setGeometry(0, 0, width(), height());
    if (m_bpDialogHost && m_bpDialogHost->isVisible())
        m_bpDialogHost->setGeometry(0, 0, width(), height());
    if (m_bpResourceBrowser && m_bpResourceBrowser->isVisible())
        m_bpResourceBrowser->setGeometry(0, 0, width(), height());
    if (m_bpKeyboard && m_bpKeyboard->isVisible()) {
        m_bpKeyboard->reposition();
        m_bpKeyboard->raise();
    }
}

void MainWindow::instanceActivated(QModelIndex index)
{
    if (!index.isValid())
        return;
    QString id = index.data(InstanceList::InstanceIDRole).toString();
    BaseInstance* inst = APPLICATION->instances()->getInstanceById(id);
    if (!inst)
        return;

    activateInstance(inst);
}

void MainWindow::on_actionLaunchInstance_triggered()
{
    if (m_selectedInstance && !m_selectedInstance->isRunning()) {
        APPLICATION->launch(m_selectedInstance);
    }
}

void MainWindow::activateInstance(BaseInstance* instance)
{
    APPLICATION->launch(instance);
}

void MainWindow::on_actionKillInstance_triggered()
{
    if (m_selectedInstance && m_selectedInstance->isRunning()) {
        APPLICATION->kill(m_selectedInstance);
    }
}

void MainWindow::on_actionCreateInstanceShortcut_triggered()
{
    if (!m_selectedInstance)
        return;

    CreateShortcutDialog shortcutDlg(m_selectedInstance, this);
    if (!shortcutDlg.exec())
        return;
    shortcutDlg.createShortcut();
}

void MainWindow::taskEnd()
{
    QObject* sender = QObject::sender();
    if (sender == m_versionLoadTask)
        m_versionLoadTask = NULL;

    sender->deleteLater();
}

void MainWindow::startTask(Task* task)
{
    connect(task, &Task::succeeded, this, &MainWindow::taskEnd);
    connect(task, &Task::failed, this, &MainWindow::taskEnd);
    task->start();
}

void MainWindow::instanceChanged(const QModelIndex& current, [[maybe_unused]] const QModelIndex& previous)
{
    if (!current.isValid()) {
        APPLICATION->settings()->set("SelectedInstance", QString());
        selectionBad();
        return;
    }
    if (m_selectedInstance) {
        disconnect(m_selectedInstance, &BaseInstance::runningStatusChanged, this, &MainWindow::refreshCurrentInstance);
        disconnect(m_selectedInstance, &BaseInstance::profilerChanged, this, &MainWindow::refreshCurrentInstance);
    }
    QString id = current.data(InstanceList::InstanceIDRole).toString();
    m_selectedInstance = APPLICATION->instances()->getInstanceById(id);
    if (m_selectedInstance) {
        ui->instanceToolBar->setEnabled(true);
        setInstanceActionsEnabled(true);
        ui->actionLaunchInstance->setEnabled(m_selectedInstance->canLaunch());

        ui->actionKillInstance->setEnabled(m_selectedInstance->isRunning());
        ui->actionExportInstance->setEnabled(m_selectedInstance->canExport());
        renameButton->setText(m_selectedInstance->name());
        m_statusLeft->setText(m_selectedInstance->getStatusbarDescription());
        updateStatusCenter();
        updateInstanceToolIcon(m_selectedInstance->iconKey());

        updateLaunchButton();

        APPLICATION->settings()->set("SelectedInstance", m_selectedInstance->id());

        connect(m_selectedInstance, &BaseInstance::runningStatusChanged, this, &MainWindow::refreshCurrentInstance);
        connect(m_selectedInstance, &BaseInstance::profilerChanged, this, &MainWindow::refreshCurrentInstance);
    } else {
        APPLICATION->settings()->set("SelectedInstance", QString());
        selectionBad();
        return;
    }
}

void MainWindow::instanceSelectRequest(QString id)
{
    setSelectedInstanceById(id);
}

void MainWindow::instanceDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight)
{
    auto current = view->selectionModel()->currentIndex();
    QItemSelection test(topLeft, bottomRight);
    if (test.contains(current)) {
        instanceChanged(current, current);
    }
}

void MainWindow::selectionBad()
{
    // start by reseting everything...
    m_selectedInstance = nullptr;
    m_statusLeft->setText(tr("No instance selected"));

    statusBar()->clearMessage();
    ui->instanceToolBar->setEnabled(false);
    setInstanceActionsEnabled(false);
    updateLaunchButton();
    renameButton->setText(tr("Rename Instance"));
    updateInstanceToolIcon("grass");

    // ...and then see if we can enable the previously selected instance
    setSelectedInstanceById(APPLICATION->settings()->get("SelectedInstance").toString());
}

void MainWindow::checkInstancePathForProblems()
{
    QString instanceFolder = APPLICATION->settings()->get("InstanceDir").toString();
    if (FS::checkProblemticPathJava(QDir(instanceFolder))) {
        QMessageBox warning(this);
        warning.setText(tr("Your instance folder contains \'!\' and this is known to cause Java problems!"));
        warning.setInformativeText(tr("You have now two options: <br/>"
                                      " - change the instance folder in the settings <br/>"
                                      " - move this installation of %1 to a different folder")
                                       .arg(BuildConfig.LAUNCHER_DISPLAYNAME));
        warning.setDefaultButton(QMessageBox::Ok);
        warning.exec();
    }
    auto tempFolderText =
        tr("This is a problem: <br/>"
           " - The launcher will likely be deleted without warning by the operating system <br/>"
           " - close the launcher now and extract it to a real location, not a temporary folder");
    QString pathfoldername = QDir(instanceFolder).absolutePath();
    if (pathfoldername.contains("Rar$", Qt::CaseInsensitive)) {
        QMessageBox warning(this);
        warning.setText(tr("Your instance folder contains \'Rar$\' - that means you haven't extracted the launcher archive!"));
        warning.setInformativeText(tempFolderText);
        warning.setDefaultButton(QMessageBox::Ok);
        warning.exec();
    } else if (pathfoldername.startsWith(QDir::tempPath()) || pathfoldername.contains("/TempState/")) {
        QMessageBox warning(this);
        warning.setText(tr("Your instance folder is in a temporary folder: \'%1\'!").arg(QDir::tempPath()));
        warning.setInformativeText(tempFolderText);
        warning.setDefaultButton(QMessageBox::Ok);
        warning.exec();
    }
}

void MainWindow::updateStatusCenter()
{
    m_statusCenter->setVisible(APPLICATION->settings()->get("ShowGlobalGameTime").toBool());

    int timePlayed = APPLICATION->instances()->getTotalPlayTime();
    if (timePlayed > 0) {
        m_statusCenter->setText(
            tr("Total playtime: %1")
                .arg(Time::prettifyDuration(timePlayed, APPLICATION->settings()->get("ShowGameTimeWithoutDays").toBool())));
    }
}
// "Instance actions" are actions that require an instance to be selected (i.e. "new instance" is not here)
// Actions that also require other conditions (e.g. a running instance) won't be changed.
void MainWindow::setInstanceActionsEnabled(bool enabled)
{
    ui->actionEditInstance->setEnabled(enabled);
    ui->actionChangeInstGroup->setEnabled(enabled);
    ui->actionViewSelectedInstFolder->setEnabled(enabled);
    ui->actionExportInstance->setEnabled(enabled);
    ui->actionDeleteInstance->setEnabled(enabled);
    ui->actionCopyInstance->setEnabled(enabled);
    ui->actionCreateInstanceShortcut->setEnabled(enabled);
}

void MainWindow::refreshCurrentInstance()
{
    auto current = view->selectionModel()->currentIndex();
    instanceChanged(current, current);
}
