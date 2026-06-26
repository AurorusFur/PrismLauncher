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

#include "InstanceWindow.h"
#include "Application.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QStatusBar>
#include <QPushButton>
#include <QScrollBar>

#include "ui/widgets/PageContainer.h"

#include "InstancePageProvider.h"

#include "icons/IconList.h"

InstanceWindow::InstanceWindow(BaseInstance* instance, QWidget* parent) : QMainWindow(parent), m_instance(instance)
{
    setAttribute(Qt::WA_DeleteOnClose);

    auto icon = APPLICATION->icons()->getIcon(m_instance->iconKey());
    QString windowTitle = tr("Console window for ") + m_instance->name();

    // Set window properties
    {
        setWindowIcon(icon);
        setWindowTitle(windowTitle);
    }

    // Add page container
    {
        auto provider = std::make_shared<InstancePageProvider>(m_instance);
        m_container = new PageContainer(provider.get(), "console", this);
        m_container->setParentContainer(this);
        setCentralWidget(m_container);
        setContentsMargins(0, 0, 0, 0);
    }

    // Add custom buttons to the page container layout.
    {
        m_buttonBar = new QWidget(this);
        auto* horizontalLayout = new QHBoxLayout(m_buttonBar);
        horizontalLayout->setObjectName(QStringLiteral("horizontalLayout"));
        horizontalLayout->setContentsMargins(0, 0, 6, 6);

        m_helpButton = new QPushButton(m_buttonBar);
        m_helpButton->setText(tr("Help"));
        horizontalLayout->addWidget(m_helpButton);
        connect(m_helpButton, &QPushButton::clicked, m_container, &PageContainer::help);

        auto* spacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);
        horizontalLayout->addSpacerItem(spacer);

        m_launchButton = new QToolButton(m_buttonBar);
        m_launchButton->setText(tr("&Launch"));
        m_launchButton->setToolTip(tr("Launch the instance"));
        m_launchButton->setPopupMode(QToolButton::MenuButtonPopup);
        m_launchButton->setMinimumWidth(80);  // HACK!!
        horizontalLayout->addWidget(m_launchButton);
        connect(m_launchButton, &QPushButton::clicked, this, [this] { APPLICATION->launch(m_instance); });

        m_killButton = new QPushButton(m_buttonBar);
        m_killButton->setText(tr("&Kill"));
        m_killButton->setToolTip(tr("Kill the running instance"));
        m_killButton->setShortcut(QKeySequence(tr("Ctrl+K")));
        horizontalLayout->addWidget(m_killButton);
        connect(m_killButton, &QPushButton::clicked, this, [this] { APPLICATION->kill(m_instance); });

        updateButtons();

        m_closeButton = new QPushButton(m_buttonBar);
        m_closeButton->setText(tr("Close"));
        horizontalLayout->addWidget(m_closeButton);
        connect(m_closeButton, &QPushButton::clicked, this, &QMainWindow::close);

        m_container->addButtons(m_buttonBar);

        connect(m_instance, &BaseInstance::profilerChanged, this, &InstanceWindow::updateButtons);
        connect(APPLICATION, &Application::globalSettingsApplied, this, &InstanceWindow::updateButtons);
    }

    // restore window state
    {
        auto base64State = APPLICATION->settings()->get("ConsoleWindowState").toString().toUtf8();
        restoreState(QByteArray::fromBase64(base64State));
        auto base64Geometry = APPLICATION->settings()->get("ConsoleWindowGeometry").toString().toUtf8();
        restoreGeometry(QByteArray::fromBase64(base64Geometry));
    }

    // set up instance and launch process recognition
    {
        auto launchTask = m_instance->getLaunchTask();
        instanceLaunchTaskChanged(launchTask);
        connect(m_instance, &BaseInstance::launchTaskChanged, this, &InstanceWindow::instanceLaunchTaskChanged);
        connect(m_instance, &BaseInstance::runningStatusChanged, this, &InstanceWindow::runningStateChanged);
    }

    // set up instance destruction detection
    {
        connect(m_instance, &BaseInstance::statusChanged, this, &InstanceWindow::on_instanceStatusChanged);
    }

    // add ourself as the modpack page's instance window
    {
        static_cast<ManagedPackPage*>(m_container->getPage("managed_pack"))->setInstanceWindow(this);
    }

    show();
}

void InstanceWindow::on_instanceStatusChanged(BaseInstance::Status, BaseInstance::Status newStatus)
{
    if (newStatus == BaseInstance::Status::Gone) {
        m_doNotSave = true;
        close();
    }
}

void InstanceWindow::updateButtons()
{
    m_launchButton->setEnabled(m_instance->canLaunch());
    m_killButton->setEnabled(m_instance->isRunning());

    QMenu* launchMenu = m_launchButton->menu();
    if (launchMenu)
        launchMenu->clear();
    else
        launchMenu = new QMenu(this);
    m_instance->populateLaunchMenu(launchMenu);
    m_launchButton->setMenu(launchMenu);
}

void InstanceWindow::instanceLaunchTaskChanged(LaunchTask* proc)
{
    m_proc = proc;
}

void InstanceWindow::runningStateChanged(bool running)
{
    updateButtons();
    m_container->refreshContainer();
    if (running) {
        selectPage("log");
    }
}

void InstanceWindow::closeEvent(QCloseEvent* event)
{
    bool proceed = true;
    if (!m_doNotSave) {
        proceed &= m_container->prepareToClose();
    }

    if (!proceed) {
        return;
    }

    APPLICATION->settings()->set("ConsoleWindowState", QString::fromUtf8(saveState().toBase64()));
    APPLICATION->settings()->set("ConsoleWindowGeometry", QString::fromUtf8(saveGeometry().toBase64()));
    emit isClosing();
    event->accept();
}

bool InstanceWindow::saveAll()
{
    return m_container->saveAll();
}

QString InstanceWindow::instanceId()
{
    return m_instance->id();
}

bool InstanceWindow::selectPage(QString pageId)
{
    return m_container->selectPage(pageId);
}

void InstanceWindow::refreshContainer()
{
    m_container->refreshContainer();
}

void InstanceWindow::navigatePage(int delta)
{
    m_container->navigatePage(delta);
}

void InstanceWindow::focusPageContent()
{
    m_container->focusFirstInContent();
}

void InstanceWindow::applyBigPictureMode()
{
    // Hide mouse-only chrome — B closes the window, so Close is redundant
    if (m_helpButton)
        m_helpButton->hide();
    if (m_closeButton)
        m_closeButton->hide();

    // Style the button bar to match the dark theme
    if (m_buttonBar) {
        m_buttonBar->setStyleSheet(
            "QWidget { background: #060c14; border-top: 1px solid #1a2a3a; }"
            "QToolButton, QPushButton {"
            "  background: #0d2035; color: #90b8d8;"
            "  border: 1px solid #1e3858; border-radius: 4px;"
            "  padding: 4px 14px; font-size: 13px;"
            "}"
            "QToolButton:hover, QPushButton:hover { background: #1a3a5a; color: #ffffff; }"
            "QToolButton:disabled, QPushButton:disabled { color: #3a5060; border-color: #111d2a; }");
    }

    // Style the sidebar and header for controller navigation
    m_container->setBigPictureMode(true);

    // Dark background for the whole window
    setStyleSheet("QMainWindow { background: #060c14; }");

    // HUD at the bottom with button hints
    statusBar()->setStyleSheet(
        "QStatusBar {"
        "  background: #040810;"
        "  border-top: 1px solid #1a2a3a;"
        "  color: #7090a8;"
        "  font-size: 13px;"
        "}");
    statusBar()->showMessage(tr("[LB / RB]  Switch Page    [↑↓]  Navigate    [A]  Select / Confirm    [Y]  Next Button    [B]  Close"));
    statusBar()->setSizeGripEnabled(false);
    statusBar()->show();
}

BasePage* InstanceWindow::selectedPage() const
{
    return m_container->selectedPage();
}

bool InstanceWindow::requestClose()
{
    if (m_container->prepareToClose()) {
        close();
        return true;
    }
    return false;
}
