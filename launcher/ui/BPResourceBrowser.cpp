// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2024 Prism Launcher Contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "BPResourceBrowser.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QPainter>
#include <QVBoxLayout>

#include "Application.h"
#include "BaseInstance.h"
#include "QObjectPtr.h"
#include "ResourceDownloadTask.h"
#include "Version.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/mod/ModFolderModel.h"
#include "modplatform/flame/FlameAPI.h"
#include "modplatform/modrinth/ModrinthAPI.h"
#include "ui/BPHud.h"
#include "ui/BPStyle.h"
#include "ui/pages/modplatform/ModModel.h"
#include "ui/widgets/ModFilterWidget.h"
#include "ui/widgets/ProjectItem.h"

BPResourceBrowser::BPResourceBrowser(QWidget* parent) : QWidget(parent)
{
    m_titleLabel = new QLabel(this);
    m_titleLabel->setTextFormat(Qt::PlainText);  // instance/pack names must never render as HTML
    {
        QFont f = m_titleLabel->font();
        f.setPixelSize(18);
        f.setBold(true);
        m_titleLabel->setFont(f);
        m_titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }

    m_statusLabel = new QLabel(this);
    m_statusLabel->setTextFormat(Qt::PlainText);
    m_statusLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    {
        QFont f = m_statusLabel->font();
        f.setPixelSize(14);
        m_statusLabel->setFont(f);
    }

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search Modrinth…"));
    m_search->setClearButtonEnabled(true);
    connect(m_search, &QLineEdit::returnPressed, this, [this] {
        runSearch();
        m_results->setFocus();
        updateHud();
    });

    m_results = new QListView(this);
    m_results->setItemDelegate(new ProjectItemDelegate(m_results));
    m_results->setSelectionMode(QAbstractItemView::SingleSelection);
    m_results->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_results->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_results->setIconSize(QSize(48, 48));  // same as the desktop downloader's list
    m_results->setAlternatingRowColors(true);
    connect(m_results, &QAbstractItemView::activated, this, [this](const QModelIndex& index) { openVersionsForRow(index.row()); });

    m_hudLabel = new QLabel(this);
    m_hudLabel->setTextFormat(Qt::RichText);
    m_hudLabel->setAlignment(Qt::AlignCenter);
    {
        QFont f = m_hudLabel->font();
        f.setPixelSize(13);
        m_hudLabel->setFont(f);
    }

    // Version picker card
    m_versionCard = new QWidget(this);
    m_versionCard->setAutoFillBackground(true);
    m_versionCard->hide();

    m_versionTitle = new QLabel(m_versionCard);
    m_versionTitle->setTextFormat(Qt::PlainText);
    m_versionTitle->setAlignment(Qt::AlignCenter);
    {
        QFont f = m_versionTitle->font();
        f.setPixelSize(15);
        f.setBold(true);
        m_versionTitle->setFont(f);
    }

    m_versionList = new QListWidget(m_versionCard);
    m_versionList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_versionList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    {
        QFont f = m_versionList->font();
        f.setPixelSize(14);
        m_versionList->setFont(f);
    }
    connect(m_versionList, &QListWidget::itemClicked, this, [this](QListWidgetItem*) { installVersion(m_versionList->currentRow()); });

    m_versionHud = new QLabel(bpHudHtml(tr("[↑↓] Version    [A] Install    [B] Back")), m_versionCard);
    m_versionHud->setTextFormat(Qt::RichText);
    m_versionHud->setAlignment(Qt::AlignCenter);
    {
        QFont f = m_versionHud->font();
        f.setPixelSize(13);
        m_versionHud->setFont(f);
    }

    auto* cardLayout = new QVBoxLayout(m_versionCard);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(0);
    cardLayout->addWidget(m_versionTitle);
    cardLayout->addWidget(m_versionList, 1);
    cardLayout->addWidget(m_versionHud);

    setStyleSheet(bpBigScreenFormStyle());
    applyTheme();
    updateHud();
    hide();
}

BPResourceBrowser::~BPResourceBrowser()
{
    if (s_instance == this)
        s_instance = nullptr;
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void BPResourceBrowser::openForMods(BaseInstance* instance, ModFolderModel* mods)
{
    m_instance = instance;
    m_mods = mods;
    m_provider = Provider::Modrinth;

    // CurseForge needs an API key at build time and loader support for this instance.
    m_curseForgeAvailable = (APPLICATION->capabilities() & Application::SupportsFlame);
    if (auto* mcInstance = dynamic_cast<MinecraftInstance*>(instance)) {
        if (auto loaders = mcInstance->getPackProfile()->getSupportedModLoaders(); loaders.has_value())
            m_curseForgeAvailable = m_curseForgeAvailable && FlameAPI::validateModLoaders(loaders.value());
    }

    m_search->clear();
    setStatus(QString());
    hideVersionCard();
    setupModel();

    if (parentWidget())
        setGeometry(parentWidget()->rect());
    show();
    raise();
    m_results->setFocus();
    updateHud();

    m_model->search();  // initial, unfiltered search
}

void BPResourceBrowser::setupModel()
{
    // Fresh model per instance/provider — search state, icons, and filters are per-instance.
    if (m_model)
        m_model->deleteLater();
    if (m_provider == Provider::CurseForge)
        m_model = new ResourceDownload::ModModel(*m_instance, new FlameAPI(), QStringLiteral("Flame"), QStringLiteral("FlameMods"));
    else
        m_model = new ResourceDownload::ModModel(*m_instance, new ModrinthAPI(), QStringLiteral("Modrinth"),
                                                 QStringLiteral("ModrinthPacks"));
    m_model->setParent(this);

    // Default filter: this instance's Minecraft version and loaders (the model
    // falls back to the instance profile's loaders when none are set here).
    auto filter = std::make_shared<ModFilterWidget::Filter>();
    filter->hideInstalled = false;
    filter->openSource = false;
    filter->side = ModPlatform::Side::NoSide;
    if (auto* mcInstance = dynamic_cast<MinecraftInstance*>(m_instance)) {
        const QString mcVersion = mcInstance->getPackProfile()->getComponentVersion("net.minecraft");
        if (!mcVersion.isEmpty())
            filter->versions.emplace_back(mcVersion);
    }
    m_model->setFilter(filter);

    m_results->setModel(m_model);
    connect(m_model, &ResourceDownload::ResourceModel::versionListUpdated, this, [this](const QModelIndex& index) {
        if (index.isValid() && index.row() == m_pendingVersionRow) {
            m_pendingVersionRow = -1;
            setStatus(QString());
            showVersionCard(index.row());
        }
    });
    // Land the selection on the first result of a fresh search.
    connect(m_model, &QAbstractItemModel::rowsInserted, this, [this] {
        if (!m_results->currentIndex().isValid() && m_model->rowCount(QModelIndex()) > 0)
            m_results->setCurrentIndex(m_model->index(0, 0));
    });

    m_pendingVersionRow = -1;
    updateTitle();
}

void BPResourceBrowser::toggleProvider()
{
    if (m_versionCard->isVisible() || !m_model)
        return;
    if (!m_curseForgeAvailable) {
        setStatus(tr("CurseForge is not available in this build or for this loader"));
        return;
    }
    m_provider = (m_provider == Provider::Modrinth) ? Provider::CurseForge : Provider::Modrinth;
    setupModel();
    m_model->setSearchTerm(m_search->text().trimmed());
    m_model->search();
    setStatus(QString());
    updateHud();
}

void BPResourceBrowser::updateTitle()
{
    const QString provider = (m_provider == Provider::CurseForge) ? tr("CurseForge") : tr("Modrinth");
    if (m_instance)
        m_titleLabel->setText(m_instance->name() + QStringLiteral("  ›  ") + tr("Download Mods — %1").arg(provider));
}

void BPResourceBrowser::closeBrowser()
{
    hideVersionCard();
    hide();
    emit browserClosing();
}

// ── Gamepad actions ───────────────────────────────────────────────────────────

static void postKey(QWidget* target, Qt::Key key)
{
    QCoreApplication::postEvent(target, new QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier));
    QCoreApplication::postEvent(target, new QKeyEvent(QEvent::KeyRelease, key, Qt::NoModifier));
}

void BPResourceBrowser::navUp()
{
    if (m_versionCard->isVisible()) {
        postKey(m_versionList, Qt::Key_Up);
        return;
    }
    if (m_search->hasFocus())
        return;
    if (m_results->currentIndex().row() <= 0) {
        focusSearch();  // ↑ from the top row lands on the search field
        return;
    }
    moveSelection(-1);
}

void BPResourceBrowser::navDown()
{
    if (m_versionCard->isVisible()) {
        postKey(m_versionList, Qt::Key_Down);
        return;
    }
    if (m_search->hasFocus()) {
        m_results->setFocus();
        updateHud();
        return;
    }
    moveSelection(+1);
}

void BPResourceBrowser::doConfirm()
{
    if (m_versionCard->isVisible()) {
        installVersion(m_versionList->currentRow());
        return;
    }
    if (m_search->hasFocus()) {
        runSearch();
        m_results->setFocus();
        updateHud();
        return;
    }
    openVersionsForRow(m_results->currentIndex().row());
}

void BPResourceBrowser::doCancel()
{
    if (m_versionCard->isVisible()) {
        hideVersionCard();
        return;
    }
    if (m_search->hasFocus()) {
        m_results->setFocus();
        updateHud();
        return;
    }
    closeBrowser();
}

void BPResourceBrowser::focusSearch()
{
    if (m_versionCard->isVisible())
        return;
    m_search->setFocus(Qt::OtherFocusReason);
    m_search->selectAll();
    updateHud();
}

void BPResourceBrowser::pageUp()
{
    if (!m_versionCard->isVisible() && !m_search->hasFocus())
        moveSelection(-8);
}

void BPResourceBrowser::pageDown()
{
    if (!m_versionCard->isVisible() && !m_search->hasFocus())
        moveSelection(+8);
}

// ── Search / navigation internals ─────────────────────────────────────────────

void BPResourceBrowser::runSearch()
{
    if (!m_model)
        return;
    m_model->searchWithTerm(m_search->text().trimmed(), 0, false);
    setStatus(QString());
}

void BPResourceBrowser::moveSelection(int delta)
{
    if (!m_model)
        return;
    const int count = m_model->rowCount(QModelIndex());
    if (count <= 0)
        return;
    const QModelIndex cur = m_results->currentIndex();
    const int row = qBound(0, (cur.isValid() ? cur.row() : 0) + delta, count - 1);
    const QModelIndex idx = m_model->index(row, 0);
    m_results->setCurrentIndex(idx);
    m_results->scrollTo(idx);
    // Nearing the end of the loaded results — ask the API for the next page.
    if (row >= count - 3 && m_model->canFetchMore(QModelIndex()))
        m_model->fetchMore(QModelIndex());
}

// ── Version picking / install ─────────────────────────────────────────────────

void BPResourceBrowser::openVersionsForRow(int row)
{
    if (!m_model || row < 0 || row >= m_model->rowCount(QModelIndex()))
        return;
    const QModelIndex idx = m_model->index(row, 0);
    auto pack = idx.data(Qt::UserRole).value<ModPlatform::IndexedPack::Ptr>();
    if (!pack)
        return;
    if (pack->versionsLoaded) {
        showVersionCard(row);
        return;
    }
    m_pendingVersionRow = row;
    setStatus(tr("Loading versions for %1…").arg(pack->name));
    m_model->loadEntry(idx);
}

void BPResourceBrowser::showVersionCard(int row)
{
    const QModelIndex idx = m_model->index(row, 0);
    auto pack = idx.data(Qt::UserRole).value<ModPlatform::IndexedPack::Ptr>();
    if (!pack || !pack->versionsLoaded)
        return;

    m_cardPack = pack;
    m_cardVersions.clear();
    // checkVersionFilters is public on ResourceModel but re-declared protected in
    // ModModel — call through the base.
    auto* baseModel = static_cast<ResourceDownload::ResourceModel*>(m_model);
    for (const auto& version : pack->versions) {
        if (version.downloadUrl.isEmpty())
            continue;  // opted-out CurseForge files can't be downloaded by third parties
        if (baseModel->checkVersionFilters(version))
            m_cardVersions << version;
    }
    if (m_cardVersions.isEmpty()) {
        setStatus(tr("No compatible versions for %1").arg(pack->name));
        return;
    }

    m_versionTitle->setText(pack->name);
    m_versionList->clear();
    for (const auto& version : m_cardVersions)
        m_versionList->addItem(version.getVersionDisplayString());
    m_versionList->setCurrentRow(0);

    positionVersionCard();
    m_versionCard->show();
    m_versionCard->raise();
    m_versionList->setFocus();
    updateHud();
}

void BPResourceBrowser::hideVersionCard()
{
    m_versionCard->hide();
    m_cardPack.reset();
    m_cardVersions.clear();
    if (isVisible())
        m_results->setFocus();
    updateHud();
}

void BPResourceBrowser::installVersion(int versionRow)
{
    if (!m_cardPack || versionRow < 0 || versionRow >= m_cardVersions.size())
        return;
    auto pack = m_cardPack;
    const auto version = m_cardVersions.at(versionRow);
    hideVersionCard();

    auto task = makeShared<ResourceDownloadTask>(pack, version, m_mods);
    setStatus(tr("Downloading %1…").arg(pack->name));
    connect(task.get(), &Task::succeeded, this, [this, pack] { setStatus(tr("Installed %1").arg(pack->name)); });
    connect(task.get(), &Task::failed, this,
            [this, pack](const QString& reason) { setStatus(tr("Failed to install %1: %2").arg(pack->name, reason)); });
    connect(task.get(), &Task::finished, this, [this, raw = task.get()] {
        m_tasks.removeIf([raw](const shared_qobject_ptr<ResourceDownloadTask>& t) { return t.get() == raw; });
    });
    m_tasks << task;
    task->start();
}

// ── Presentation ──────────────────────────────────────────────────────────────

void BPResourceBrowser::setStatus(const QString& text)
{
    m_statusLabel->setText(text);
}

void BPResourceBrowser::updateHud()
{
    QString hint;
    if (m_versionCard->isVisible())
        hint = tr("[↑↓] Version    [A] Install    [B] Back");
    else if (m_search->hasFocus())
        hint = tr("Type with a keyboard    [A] Search    [↓/B] Back to Results");
    else if (m_curseForgeAvailable)
        hint = tr("[↑↓] Navigate    [A] Versions    [X] Search    [Y] Source    [LB/RB] Page    [B] Close");
    else
        hint = tr("[↑↓] Navigate    [A] Versions    [X] Search    [LB/RB] Page    [B] Close");
    m_hudLabel->setText(bpHudHtml(hint));
}

void BPResourceBrowser::relayout()
{
    const int w = width();
    const int h = height();

    m_titleLabel->setGeometry(MARGIN, 0, w / 2 - MARGIN, TITLE_H);
    m_statusLabel->setGeometry(w / 2, 0, w / 2 - MARGIN, TITLE_H);
    m_search->setGeometry(MARGIN, TITLE_H + 8, w - 2 * MARGIN, SEARCH_H);
    const int listY = TITLE_H + 8 + SEARCH_H + 8;
    m_results->setGeometry(MARGIN, listY, w - 2 * MARGIN, h - listY - HUD_H - 8);
    m_hudLabel->setGeometry(0, h - HUD_H, w, HUD_H);

    if (m_versionCard->isVisible())
        positionVersionCard();
}

void BPResourceBrowser::positionVersionCard()
{
    const int cardW = qMin(560, width() - 80);
    const int itemH = 40;
    const int titleH = 44;
    const int hudH = 36;
    const int items = qMin(m_versionList->count(), 10);
    const int cardH = qMin(titleH + qMax(items, 1) * itemH + hudH, height() - 120);
    m_versionCard->setGeometry((width() - cardW) / 2, (height() - cardH) / 2, cardW, cardH);
    for (int i = 0; i < m_versionList->count(); ++i)
        if (auto* item = m_versionList->item(i))
            item->setSizeHint(QSize(cardW, itemH));
    m_versionTitle->setFixedHeight(titleH);
    m_versionHud->setFixedHeight(hudH);
}

void BPResourceBrowser::applyTheme()
{
    const QPalette& pal = QApplication::palette();
    const QColor base = pal.color(QPalette::Base);
    const QColor mid = pal.color(QPalette::Mid);
    const QColor text = pal.color(QPalette::WindowText);
    const QColor hl = pal.color(QPalette::Highlight);
    const QColor hlText = pal.color(QPalette::HighlightedText);
    const QColor headerBg = base.darker(110);

    m_titleLabel->setStyleSheet(QString("QLabel { color: %1; background: transparent; }").arg(text.name()));
    m_statusLabel->setStyleSheet(QString("QLabel { color: %1; background: transparent; }").arg(hl.lighter(130).name()));
    m_hudLabel->setStyleSheet(QString("QLabel { background: %1; border-top: 1px solid %2; color: %3; }")
                                  .arg(base.darker(115).name(), mid.name(), text.name()));
    // font-size in *points*, not pixels: ProjectItemDelegate derives the title font
    // via font.pointSize() + 2, and pointSize() is -1 for pixel-specified fonts,
    // which would shrink titles to 1pt (invisible).
    m_results->setStyleSheet(
        QString("QListView { background: %1; border: 1px solid %2; border-radius: 6px; outline: none; font-size: 11pt; }")
            .arg(base.name(), mid.name()));

    m_versionCard->setStyleSheet(
        QString("QWidget { background: %1; border: 1px solid %2; border-radius: 10px; }").arg(base.name(), mid.name()));
    m_versionTitle->setStyleSheet(
        QString("QLabel { color: %1; background: %2; border: none; border-bottom: 1px solid %3; padding: 8px;"
                "  border-top-left-radius: 10px; border-top-right-radius: 10px;"
                "  border-bottom-left-radius: 0; border-bottom-right-radius: 0; }")
            .arg(text.name(), headerBg.name(), mid.name()));
    m_versionList->setStyleSheet(QString("QListWidget { background: %1; border: none; border-radius: 0; outline: none; }"
                                         "QListWidget::item { padding: 8px 16px; color: %2; border-left: 4px solid transparent; }"
                                         "QListWidget::item:selected:active, QListWidget::item:selected:!active"
                                         "  { background: %3; color: %4; border-left: 4px solid %5; }")
                                     .arg(base.name(), text.name(), hl.name(), hlText.name(), hl.lighter(160).name()));
    m_versionHud->setStyleSheet(QString("QLabel { color: %1; background: %2; border: none; border-top: 1px solid %3; padding: 6px;"
                                        "  border-bottom-left-radius: 10px; border-bottom-right-radius: 10px;"
                                        "  border-top-left-radius: 0; border-top-right-radius: 0; }")
                                    .arg(text.name(), headerBg.name(), mid.name()));
}

void BPResourceBrowser::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QPalette& pal = QApplication::palette();
    p.fillRect(rect(), pal.color(QPalette::Window));
    p.fillRect(0, 0, width(), TITLE_H, pal.color(QPalette::Base));
    p.fillRect(0, TITLE_H - 1, width(), 1, pal.color(QPalette::Mid));
}

void BPResourceBrowser::resizeEvent(QResizeEvent* ev)
{
    QWidget::resizeEvent(ev);
    relayout();
}

void BPResourceBrowser::changeEvent(QEvent* ev)
{
    if (ev->type() == QEvent::PaletteChange)
        applyTheme();
    QWidget::changeEvent(ev);
}
