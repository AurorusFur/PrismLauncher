// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2024 Prism Launcher Contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "BPSettingsOverlay.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QPalette>
#include <QResizeEvent>
#include <QSet>
#include <QShowEvent>

#include "BaseInstance.h"
#include "InstancePageProvider.h"
#include "ui/pages/BasePage.h"
#include "ui/widgets/PageContainer.h"

// Pages hidden from the sidebar (too niche for controller navigation)
static const QSet<QString> kHiddenPageIds = { "coremods", "nilmods" };

// ── Constructor / destructor ──────────────────────────────────────────────────

BPSettingsOverlay::BPSettingsOverlay(QWidget* parent) : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    hide();

    // Title label (instance name)
    m_titleLabel = new QLabel(this);
    {
        QFont f = m_titleLabel->font();
        f.setPixelSize(18);
        f.setBold(true);
        m_titleLabel->setFont(f);
        m_titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }

    // Sidebar list
    m_sidebar = new QListWidget(this);
    m_sidebar->setFocusPolicy(Qt::StrongFocus);
    m_sidebar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sidebar->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_sidebar->setSelectionMode(QAbstractItemView::SingleSelection);
    {
        QFont f = m_sidebar->font();
        f.setPixelSize(15);
        m_sidebar->setFont(f);
    }

    // HUD strip
    m_hudLabel = new QLabel(this);
    m_hudLabel->setAlignment(Qt::AlignCenter);
    {
        QFont f = m_hudLabel->font();
        f.setPixelSize(13);
        m_hudLabel->setFont(f);
    }

    applyTheme();
    updateHud();
}

BPSettingsOverlay::~BPSettingsOverlay()
{
    teardown();
}

// ── Public interface ──────────────────────────────────────────────────────────

void BPSettingsOverlay::open(BaseInstance* instance)
{
    if (m_instance != instance || !m_container) {
        teardown();
        rebuild(instance);
    }
    m_instance = instance;
    if (parentWidget())
        setGeometry(0, 0, parentWidget()->width(), parentWidget()->height());
    setMode(Mode::TabBar);
    show();
    raise();
}

void BPSettingsOverlay::closeOverlay()
{
    if (m_container)
        m_container->prepareToClose();
    hide();
    emit overlayClosing();
}

void BPSettingsOverlay::tabLeft()
{
    if (m_filteredPages.isEmpty()) return;
    int next = (m_currentTab - 1 + m_filteredPages.size()) % m_filteredPages.size();
    bool stayInContent = (m_mode == Mode::Content);
    switchToTabIndex(next, stayInContent);
}

void BPSettingsOverlay::tabRight()
{
    if (m_filteredPages.isEmpty()) return;
    int next = (m_currentTab + 1) % m_filteredPages.size();
    bool stayInContent = (m_mode == Mode::Content);
    switchToTabIndex(next, stayInContent);
}

void BPSettingsOverlay::enterContent()
{
    setMode(Mode::Content);
    focusPageContent();
}

void BPSettingsOverlay::exitToTabBar()
{
    setMode(Mode::TabBar);
}

void BPSettingsOverlay::doConfirm()
{
    QWidget* fw = QApplication::focusWidget();
    if (!fw || !isAncestorOf(fw)) fw = this;
    QCoreApplication::postEvent(fw, new QKeyEvent(QEvent::KeyPress,   Qt::Key_Return, Qt::NoModifier));
    QCoreApplication::postEvent(fw, new QKeyEvent(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier));
}

void BPSettingsOverlay::doTabKey()
{
    QWidget* fw = QApplication::focusWidget();
    if (!fw || !isAncestorOf(fw)) fw = this;
    QCoreApplication::postEvent(fw, new QKeyEvent(QEvent::KeyPress,   Qt::Key_Tab, Qt::NoModifier));
    QCoreApplication::postEvent(fw, new QKeyEvent(QEvent::KeyRelease, Qt::Key_Tab, Qt::NoModifier));
}

void BPSettingsOverlay::sendKeyToFocused(Qt::Key key)
{
    QWidget* fw = QApplication::focusWidget();
    if (!fw || !isAncestorOf(fw)) fw = this;
    QCoreApplication::postEvent(fw, new QKeyEvent(QEvent::KeyPress,   key, Qt::NoModifier));
    QCoreApplication::postEvent(fw, new QKeyEvent(QEvent::KeyRelease, key, Qt::NoModifier));
}

// ── Page category helpers ─────────────────────────────────────────────────────

BPSettingsOverlay::PageCategory BPSettingsOverlay::currentPageCategory() const
{
    if (!m_container) return PageCategory::Other;
    auto* page = m_container->selectedPage();
    if (!page) return PageCategory::Other;
    const QString id = page->id();

    static const QSet<QString> extRes = {
        "mods", "coremods", "nilmods", "resourcepacks",
        "texturepacks", "shaderpacks", "datapacks"
    };
    if (extRes.contains(id)) return PageCategory::ExternalResource;
    if (id == "version")     return PageCategory::Version;
    if (id == "worlds")      return PageCategory::WorldList;
    if (id == "servers")     return PageCategory::Servers;
    if (id == "screenshots") return PageCategory::Screenshots;
    if (id == "settings" || id == "notes" || id == "gameoptions" || id == "managed_pack")
        return PageCategory::Settings;
    if (id == "console")     return PageCategory::Log;
    return PageCategory::Other;
}

bool BPSettingsOverlay::triggerPageAction(const QString& actionName)
{
    if (!m_container) return false;
    auto* page = dynamic_cast<QWidget*>(m_container->selectedPage());
    if (!page) return false;
    if (auto* action = page->findChild<QAction*>(actionName)) {
        if (action->isEnabled()) {
            action->trigger();
            return true;
        }
    }
    return false;
}

void BPSettingsOverlay::triggerPrimaryAction()
{
    switch (currentPageCategory()) {
        case PageCategory::ExternalResource:
            // Prefer online browser; fall back to file add
            if (!triggerPageAction("actionDownloadItem"))
                triggerPageAction("actionAddItem");
            break;
        case PageCategory::Version:
            triggerPageAction("actionInstall_Loader");
            break;
        case PageCategory::WorldList:
        case PageCategory::Servers:
            triggerPageAction("actionAdd");
            break;
        case PageCategory::Screenshots:
            triggerPageAction("actionCopy_Image");
            break;
        default:
            doTabKey();
            break;
    }
}

void BPSettingsOverlay::triggerSecondaryAction()
{
    switch (currentPageCategory()) {
        case PageCategory::ExternalResource:
            triggerPageAction("actionRemoveItem");
            break;
        case PageCategory::Version:
        case PageCategory::WorldList:
        case PageCategory::Servers:
            triggerPageAction("actionRemove");
            break;
        case PageCategory::Screenshots:
            triggerPageAction("actionDelete");
            break;
        default:
            break;
    }
}

void BPSettingsOverlay::triggerToggleAction()
{
    switch (currentPageCategory()) {
        case PageCategory::ExternalResource:
            sendKeyToFocused(Qt::Key_Space);  // toggle checkbox
            break;
        case PageCategory::Servers:
            triggerPageAction("actionJoin");
            break;
        default:
            break;
    }
}

// ── Private implementation ────────────────────────────────────────────────────

void BPSettingsOverlay::rebuild(BaseInstance* instance)
{
    m_provider  = std::make_shared<InstancePageProvider>(instance);
    m_container = new PageContainer(m_provider.get(), "settings", this);
    m_container->hidePageList();
    m_container->setBigPictureMode(true);

    // Build filtered page list (skip hidden IDs and pages that shouldn't show)
    m_filteredPages.clear();
    for (auto* page : m_container->getPages()) {
        if (!page->shouldDisplay()) continue;
        if (kHiddenPageIds.contains(page->id())) continue;
        m_filteredPages << page;
    }

    // Populate sidebar
    m_sidebar->blockSignals(true);
    m_sidebar->clear();
    for (auto* page : m_filteredPages) {
        auto* item = new QListWidgetItem(page->displayName(), m_sidebar);
        item->setSizeHint(QSize(SIDEBAR_W, 46));
    }
    m_sidebar->blockSignals(false);

    // Sync m_currentTab to whichever page the container opened on
    m_currentTab = 0;
    if (auto* cur = m_container->selectedPage()) {
        for (int i = 0; i < m_filteredPages.size(); ++i) {
            if (m_filteredPages[i] == cur) { m_currentTab = i; break; }
        }
    }
    m_sidebar->setCurrentRow(m_currentTab);

    // Page changes fired from sidebar navigation update the container
    connect(m_sidebar, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0 || row >= m_filteredPages.size()) return;
        m_currentTab = row;
        m_container->selectPage(m_filteredPages[row]->id());
        updateHud();
    });

    // Update HUD when container switches page (e.g. via LB/RB in content mode)
    connect(m_container, &PageContainer::selectedPageChanged, this, [this](BasePage*, BasePage*) {
        updateHud();
    });

    m_titleLabel->setText(instance->name());
    relayout();
}

void BPSettingsOverlay::teardown()
{
    if (m_container) {
        m_container->prepareToClose();
        delete m_container;
        m_container = nullptr;
    }
    m_sidebar->blockSignals(true);
    m_sidebar->clear();
    m_sidebar->blockSignals(false);
    m_filteredPages.clear();
    m_provider.reset();
    m_instance   = nullptr;
    m_currentTab = 0;
}

void BPSettingsOverlay::switchToTabIndex(int index, bool focusContent)
{
    if (index < 0 || index >= m_filteredPages.size()) return;
    m_currentTab = index;
    // Updating the sidebar row fires currentRowChanged which calls selectPage()
    m_sidebar->blockSignals(true);
    m_sidebar->setCurrentRow(index);
    m_sidebar->blockSignals(false);
    m_container->selectPage(m_filteredPages[index]->id());
    updateHud();

    if (focusContent) {
        if (m_mode != Mode::Content)
            setMode(Mode::Content);
        focusPageContent();
    }
}

void BPSettingsOverlay::setMode(Mode m)
{
    m_mode = m;
    if (m == Mode::TabBar) {
        m_sidebar->setFocus();
    } else {
        focusPageContent();
    }
    updateHud();
}

void BPSettingsOverlay::updateHud()
{
    if (!m_hudLabel) return;
    if (m_mode == Mode::TabBar) {
        m_hudLabel->setText(tr("[↑↓] Select Page    [→ / A] Enter    [LB/RB] Prev/Next    [B] Close"));
        return;
    }
    switch (currentPageCategory()) {
        case PageCategory::ExternalResource:
            m_hudLabel->setText(tr("[↑↓] Navigate    [A] Select    [Start] Toggle    [X] Remove    [Y] Download    [LB/RB] Page    [B] Back"));
            break;
        case PageCategory::Version:
            m_hudLabel->setText(tr("[↑↓] Navigate    [A] Select    [X] Remove    [Y] Install Loader    [LB/RB] Page    [B] Back"));
            break;
        case PageCategory::WorldList:
            m_hudLabel->setText(tr("[↑↓] Navigate    [A] Open    [X] Delete    [Y] Add    [LB/RB] Page    [B] Back"));
            break;
        case PageCategory::Servers:
            m_hudLabel->setText(tr("[↑↓] Navigate    [Start] Join    [X] Remove    [Y] Add    [LB/RB] Page    [B] Back"));
            break;
        case PageCategory::Screenshots:
            m_hudLabel->setText(tr("[↑↓] Navigate    [A] Open    [X] Delete    [Y] Copy    [LB/RB] Page    [B] Back"));
            break;
        case PageCategory::Settings:
            m_hudLabel->setText(tr("[↑↓] Navigate    [A] Edit    [Y] Next Field    [LB/RB] Page    [B] Back"));
            break;
        case PageCategory::Log:
            m_hudLabel->setText(tr("[↑↓] Scroll    [LB/RB] Page    [B] Back to Pages"));
            break;
        default:
            m_hudLabel->setText(tr("[↑↓/←→] Navigate    [A] Select    [Y] Next Button    [LB/RB] Page    [B] Back"));
            break;
    }
}

void BPSettingsOverlay::applyTheme()
{
    const QPalette& pal = QApplication::palette();
    const QColor window   = pal.color(QPalette::Window);
    const QColor base     = pal.color(QPalette::Base);
    const QColor hl       = pal.color(QPalette::Highlight);
    const QColor hlText   = pal.color(QPalette::HighlightedText);
    const QColor text     = pal.color(QPalette::WindowText);
    const QColor midText  = pal.color(QPalette::Mid).lighter(130);

    // Slightly different shade for sidebar vs content background
    const QColor sidebarBg = base.darker(110);

    m_titleLabel->setStyleSheet(
        QString("QLabel { color: %1; background: transparent; }").arg(text.name()));

    m_sidebar->setStyleSheet(
        QString(
            "QListWidget {"
            "  background: %1;"
            "  border: none; outline: none;"
            "}"
            "QListWidget::item {"
            "  padding: 10px 16px 10px 18px;"
            "  color: %2;"
            "  border-left: 4px solid transparent;"
            "}"
            "QListWidget::item:selected:active,"
            "QListWidget::item:selected:!active {"
            "  background: %3;"
            "  color: %4;"
            "  border-left: 4px solid %5;"
            "}"
            "QListWidget::item:hover:!selected {"
            "  background: %6;"
            "}"
        )
        .arg(sidebarBg.name())
        .arg(text.name())
        .arg(hl.name())
        .arg(hlText.name())
        .arg(hl.lighter(160).name())   // bright accent strip on selected item
        .arg(window.name())             // subtle hover
    );

    m_hudLabel->setStyleSheet(
        QString(
            "QLabel {"
            "  background: %1;"
            "  border-top: 1px solid %2;"
            "  color: %3;"
            "}"
        )
        .arg(base.darker(115).name())
        .arg(pal.color(QPalette::Mid).name())
        .arg(midText.name())
    );
}

void BPSettingsOverlay::focusPageContent()
{
    if (m_container)
        m_container->focusFirstInContent();
}

void BPSettingsOverlay::relayout()
{
    if (!isVisible() && !m_container) return;
    const int w = width();
    const int h = height();

    m_titleLabel->setGeometry(SIDEBAR_W + 20, 0, w - SIDEBAR_W - 40, TITLE_H);

    const int listH = h - TITLE_H - HUD_H;
    m_sidebar->setGeometry(0, TITLE_H, SIDEBAR_W, listH);

    if (m_container)
        m_container->setGeometry(SIDEBAR_W + 1, TITLE_H, w - SIDEBAR_W - 1, listH);

    m_hudLabel->setGeometry(0, h - HUD_H, w, HUD_H);
}

// ── Qt event overrides ────────────────────────────────────────────────────────

void BPSettingsOverlay::showEvent(QShowEvent* ev)
{
    if (parentWidget())
        setGeometry(0, 0, parentWidget()->width(), parentWidget()->height());
    QWidget::showEvent(ev);
    relayout();
}

void BPSettingsOverlay::resizeEvent(QResizeEvent* ev)
{
    QWidget::resizeEvent(ev);
    relayout();
}

void BPSettingsOverlay::changeEvent(QEvent* ev)
{
    if (ev->type() == QEvent::PaletteChange)
        applyTheme();
    QWidget::changeEvent(ev);
}

void BPSettingsOverlay::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QPalette& pal = QApplication::palette();

    // Full background
    p.fillRect(rect(), pal.color(QPalette::Window));

    // Title band across full width
    p.fillRect(0, 0, width(), TITLE_H, pal.color(QPalette::Base));

    // Separator under title
    p.fillRect(0, TITLE_H - 1, width(), 1, pal.color(QPalette::Mid));

    // Sidebar background column (QListWidget is on top but paint the strip for completeness)
    const int listH = height() - TITLE_H - HUD_H;
    p.fillRect(0, TITLE_H, SIDEBAR_W, listH, pal.color(QPalette::Base).darker(110));

    // Vertical separator between sidebar and content
    p.fillRect(SIDEBAR_W, TITLE_H, 1, listH, pal.color(QPalette::Mid));
}
