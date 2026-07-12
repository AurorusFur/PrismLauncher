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

#include <algorithm>

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QEventLoop>
#include <QKeyEvent>
#include <QButtonGroup>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QTabBar>
#include <QPainter>
#include <QPalette>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSet>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QTabWidget>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidgetAction>

#include "Application.h"
#include "BaseInstance.h"
#include "InstancePageProvider.h"
#include "ui/BPAnim.h"
#include "ui/BPFocusRing.h"
#include "ui/BPHud.h"
#include "ui/BPNav.h"
#include "ui/BPStyle.h"
#include "ui/BPVirtualKeyboard.h"
#include "ui/pages/BasePage.h"
#include "ui/widgets/PageContainer.h"

static const QSet<QString> kHiddenPageIds = { "coremods", "nilmods" };

// Forward-declare helpers used inside the constructor lambda.
static bool isPopupOpen();

// ── Constructor / destructor ──────────────────────────────────────────────────

BPSettingsOverlay::BPSettingsOverlay(QWidget* parent) : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    hide();

    m_titleLabel = new QLabel(this);
    {
        QFont f = m_titleLabel->font();
        f.setPixelSize(18);
        f.setBold(true);
        m_titleLabel->setFont(f);
        m_titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }

    m_sidebar = new QListWidget(this);
    m_sidebar->setFocusPolicy(Qt::StrongFocus);
    m_sidebar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sidebar->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_sidebar->setSelectionMode(QAbstractItemView::SingleSelection);
    m_sidebar->setIconSize(QSize(20, 20));
    {
        QFont f = m_sidebar->font();
        f.setPixelSize(15);
        m_sidebar->setFont(f);
    }

    m_hudLabel = new QLabel(this);
    m_hudLabel->setTextFormat(Qt::RichText);
    m_hudLabel->setAlignment(Qt::AlignCenter);
    {
        QFont f = m_hudLabel->font();
        f.setPixelSize(13);
        m_hudLabel->setFont(f);
    }

    // Dim the page behind modal cards (action menu / prompt) — created before the
    // cards so it stacks underneath them.
    m_modalScrim = new QWidget(this);
    m_modalScrim->setAttribute(Qt::WA_StyledBackground);
    m_modalScrim->setStyleSheet(QStringLiteral("background: rgba(0, 0, 0, 140);"));
    m_modalScrim->hide();

    buildActionPopup();
    buildPromptCard();
    buildHelpWidget();

    m_focusRing = new FocusRingWidget(this);
    // The ring is repositioned event-driven from focusChanged below; this slow
    // poll is only a fallback for moves with no signal to hook (scrolling under
    // a fixed focus widget, model resets, async page updates).
    m_ringTimer = new QTimer(this);
    m_ringTimer->setInterval(200);
    connect(m_ringTimer, &QTimer::timeout, this, [this] {
        updateFocusRing();
        // Resource folder models fill asynchronously — the view is often focused
        // while it still has 0 rows, so the ensure in focusPageContent() ran on
        // an empty list. Land on the first row as soon as one exists; without a
        // current row all row-dependent actions (remove, edit…) stay disabled.
        auto* c = currentContainer();
        QWidget* fw = QApplication::focusWidget();
        if (auto* view = qobject_cast<QAbstractItemView*>(fw); view && c && c->isAncestorOf(view))
            bpEnsureCurrentRow(view);
    });

    applyTheme();
    updateHud();

    // Update HUD and handle special focus transitions in Content mode.
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* now) {
        updateFocusRing();  // event-driven; cheap early-outs when hidden/irrelevant
        // Value-edit mode is bound to one widget; any focus move ends it.
        if (m_editWidget && now != m_editWidget)
            leaveValueEdit();
        // When a combo popup opens, focus moves outside our tree — update HUD for popup hints.
        if (m_mode == Mode::Content && now && !isAncestorOf(now)) {
            if (isPopupOpen()) updateHud();
            return;
        }
        if (!now || !isAncestorOf(now)) return;
        // Scroll focused widget into view if inside a QScrollArea.
        for (QWidget* p = now->parentWidget(); p && p != this; p = p->parentWidget()) {
            if (auto* sa = qobject_cast<QScrollArea*>(p)) {
                sa->ensureWidgetVisible(now);
                break;
            }
        }
        // Auto-skip anything a controller can't use (labels, viewports, text fields…)
        // when Qt or page code lands focus there — move on to the next real control.
        if (m_mode == Mode::Content) {
            auto* c = currentContainer();
            if (c && c->isAncestorOf(now) && now != c && !bpControllerFocusable(now, c)) {
                focusNextInContent(true);
                return;
            }
            updateHud();
        }
    });
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
    show();
    raise();
    bpSlideIn(this);
    setMode(Mode::TabBar);  // after show() so setFocus() takes effect; also arms the ring timer
    IBigPicturePrompt::setInstance(this);
}

void BPSettingsOverlay::prewarm(BaseInstance* instance)
{
    if (!instance || isVisible())
        return;
    if (m_instance == instance && m_container)
        return;  // already built for this instance
    teardown();
    rebuild(instance);
    m_instance = instance;
    // Built but still hidden — open() will now find m_container and skip rebuild.
}

void BPSettingsOverlay::closeOverlay()
{
    IBigPicturePrompt::setInstance(nullptr);
    // Take the on-screen keyboard down with us if it's editing one of our fields.
    if (auto* kb = BPVirtualKeyboard::activeInstance(); kb && kb->isVisible() && kb->target() && isAncestorOf(kb->target()))
        kb->dismissSilently();
    if (m_ringTimer) m_ringTimer->stop();
    if (m_focusRing) m_focusRing->hide();
    if (m_promptLoop) {
        m_promptResult = -1;
        m_promptLoop->quit();
    }
    if (m_globalContainer)
        m_globalContainer->saveAll();
    if (m_container)
        m_container->prepareToClose();

    // Defer globalSettingsApplied until after the caller finishes (and potentially deletes us).
    // Emitting synchronously here causes MainWindow::globalSettingsClosed → applyBigPictureMode
    // to call closeOverlay() re-entrantly, which crashes when BigPictureMode was toggled off.
    // Only fire it if the user actually navigated into global settings this session.
    if (m_globalSettingsVisited) {
        m_globalSettingsVisited = false;
        QTimer::singleShot(0, APPLICATION, &Application::emitGlobalSettingsApplied);
    }

    hide();
    emit overlayClosing();
}

void BPSettingsOverlay::tabLeft()
{
    if (m_mode == Mode::Content && cycleInnerTab(-1)) return;
    if (m_filteredPages.isEmpty()) return;
    int next = (m_currentTab - 1 + m_filteredPages.size()) % m_filteredPages.size();
    bool stay = (m_mode == Mode::Content);
    switchToTabIndex(next, stay);
}

void BPSettingsOverlay::tabRight()
{
    if (m_mode == Mode::Content && cycleInnerTab(+1)) return;
    if (m_filteredPages.isEmpty()) return;
    int next = (m_currentTab + 1) % m_filteredPages.size();
    bool stay = (m_mode == Mode::Content);
    switchToTabIndex(next, stay);
}

// Pages like Instance Settings contain their own QTabWidget (General / Java / Tweaks…).
// In Content mode LB/RB cycles those inner tabs; pages without one fall back to
// switching overlay pages. Returns the tab widget or nullptr.
QTabWidget* BPSettingsOverlay::innerTabWidget() const
{
    if (m_helpShowing) return nullptr;
    auto* c = currentContainer();
    if (!c) return nullptr;
    auto* page = dynamic_cast<QWidget*>(c->selectedPage());
    if (!page) return nullptr;
    auto* tabs = page->findChild<QTabWidget*>();
    if (tabs && tabs->isVisibleTo(page) && tabs->count() > 1)
        return tabs;
    return nullptr;
}

bool BPSettingsOverlay::cycleInnerTab(int delta)
{
    auto* tabs = innerTabWidget();
    if (!tabs) return false;
    const int n = tabs->count();
    tabs->setCurrentIndex((tabs->currentIndex() + delta + n) % n);
    m_orderCachePage = nullptr;  // visible widget set changed within the same page
    focusPageContent();  // land on the first control of the newly shown tab
    updateHud();
    return true;
}

// ── Directional navigation ────────────────────────────────────────────────────

// Returns true for "form" widgets where D-pad Up/Down should move focus to the
// prev/next widget rather than scroll or change a value. Only item views keep
// native Up/Down (row navigation). Everything else — including spinboxes and
// text editors — only gets native keys while in explicit edit mode.
static bool isFormWidget(QWidget* w)
{
    return w && !qobject_cast<QAbstractItemView*>(w);
}

// Widgets whose native Up/Down/typing behavior is gated behind explicit edit
// mode (A to enter, A/B to leave) so focus traversal can't change their value
// and the user can't get stuck inside them.
static bool needsEditMode(QWidget* w)
{
    return qobject_cast<QAbstractSpinBox*>(w) || qobject_cast<QLineEdit*>(w) ||
           qobject_cast<QTextEdit*>(w) || qobject_cast<QPlainTextEdit*>(w);
}

// Returns true when a Qt Popup window (e.g. a QComboBox dropdown) is active.
// Focus is then outside our widget tree, requiring direct dispatch to the popup.
static bool isPopupOpen()
{
    QWidget* fw = QApplication::focusWidget();
    if (!fw) return false;
    for (QWidget* p = fw; p; p = p->parentWidget()) {
        if (p->windowType() == Qt::Popup)
            return true;
    }
    return false;
}

// Cycle to the prev(-1) or next(+1) QRadioButton sibling within the same parent.
static void cycleRadioButton(QRadioButton* rb, int delta)
{
    QWidget* parent = rb->parentWidget();
    if (!parent) return;
    QList<QRadioButton*> siblings;
    for (auto* s : parent->findChildren<QRadioButton*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (s->isVisibleTo(parent) && s->isEnabled())
            siblings.append(s);
    }
    int idx = siblings.indexOf(rb);
    if (idx < 0 || siblings.size() < 2) return;
    int next = (idx + delta + siblings.size()) % siblings.size();
    siblings[next]->setFocus();
    siblings[next]->setChecked(true);
}


// All controller-usable widgets of the current page in *visual* order:
// top-to-bottom, then left-to-right within a row. Qt's declared tab order often
// disagrees with the layout (Down jumping left or back to the top), which is
// disorienting with a D-pad — so navigation is driven by geometry instead.
QList<QWidget*> BPSettingsOverlay::orderedContentWidgets() const
{
    auto* c = currentContainer();
    if (!c) return {};
    auto* page = dynamic_cast<QWidget*>(c->selectedPage());
    if (!page) return {};

    // Burst cache: a single navigation event calls this two or three times (the
    // nav slot, then the focusChanged auto-skip). findChildren over a whole
    // settings page plus a geometry sort per keypress adds up under held
    // auto-repeat. 100 ms is long enough to cover one event burst and too short
    // for the page's widget set to change under the user.
    if (page == m_orderCachePage && m_orderCacheTime.isValid() && m_orderCacheTime.elapsed() < 100)
        return m_orderCache;

    const QList<QWidget*> list = bpOrderedControllerWidgets(page, c);

    m_orderCachePage = page;
    m_orderCache = list;
    m_orderCacheTime.start();
    return list;
}

// Move focus to the next/previous controller-usable widget in visual order,
// wrapping from the last field straight back to the first.
void BPSettingsOverlay::focusNextInContent(bool forward)
{
    const QList<QWidget*> order = orderedContentWidgets();
    if (order.isEmpty()) return;
    QWidget* fw = QApplication::focusWidget();
    int idx = order.indexOf(fw);
    if (idx < 0) {
        order.first()->setFocus(Qt::OtherFocusReason);
        return;
    }
    idx = (idx + (forward ? 1 : -1) + order.size()) % order.size();
    order[idx]->setFocus(forward ? Qt::TabFocusReason : Qt::BacktabFocusReason);
}

QWidget* BPSettingsOverlay::contentFocusWidget() const
{
    auto* c = currentContainer();
    QWidget* fw = QApplication::focusWidget();
    if (fw && c && c->isAncestorOf(fw))
        return fw;
    return c;
}

void BPSettingsOverlay::sidebarNavUp()
{
    int row = m_sidebar->currentRow() - 1;
    while (row >= 0) {
        auto* it = m_sidebar->item(row);
        if (it && (it->flags() & Qt::ItemIsSelectable)) break;
        --row;
    }
    if (row >= 0) m_sidebar->setCurrentRow(row);
}

void BPSettingsOverlay::sidebarNavDown()
{
    int row = m_sidebar->currentRow() + 1;
    const int total = m_sidebar->count();
    while (row < total) {
        auto* it = m_sidebar->item(row);
        if (it && (it->flags() & Qt::ItemIsSelectable)) break;
        ++row;
    }
    if (row < total) m_sidebar->setCurrentRow(row);
}

void BPSettingsOverlay::navUp()
{
    switch (m_mode) {
        case Mode::TabBar:     sidebarNavUp(); break;
        case Mode::Content: {
            if (isPopupOpen()) {
                if (auto* fw = QApplication::focusWidget()) bpPostKey(fw, Qt::Key_Up);
                break;
            }
            auto* w = contentFocusWidget();
            if (!w) break;
            // A view without a current row eats arrow keys on some styles —
            // make the first press land on a row instead.
            if (auto* view = qobject_cast<QAbstractItemView*>(w); view && !view->currentIndex().isValid()) {
                bpEnsureCurrentRow(view);
                break;
            }
            if (w != m_editWidget && isFormWidget(w))
                focusNextInContent(false);
            else
                bpPostKey(w, Qt::Key_Up);
            break;
        }
        case Mode::ActionMenu: bpPostKey(m_actionList, Qt::Key_Up); break;
        case Mode::PromptCard: bpPostKey(m_promptList, Qt::Key_Up); break;
    }
}

void BPSettingsOverlay::navDown()
{
    switch (m_mode) {
        case Mode::TabBar:     sidebarNavDown(); break;
        case Mode::Content: {
            if (isPopupOpen()) {
                if (auto* fw = QApplication::focusWidget()) bpPostKey(fw, Qt::Key_Down);
                break;
            }
            auto* w = contentFocusWidget();
            if (!w) break;
            if (auto* view = qobject_cast<QAbstractItemView*>(w); view && !view->currentIndex().isValid()) {
                bpEnsureCurrentRow(view);  // see navUp
                break;
            }
            if (w != m_editWidget && isFormWidget(w)) {
                focusNextInContent(true);
                break;
            }
            // Servers page: the edit fields sit below the list, so ↓ on the last
            // row continues into them instead of dead-ending in the list.
            if (auto* view = qobject_cast<QAbstractItemView*>(w);
                view && currentPageCategory() == PageCategory::Servers && view->currentIndex().isValid() &&
                view->currentIndex().row() == view->model()->rowCount(view->rootIndex()) - 1) {
                focusNextInContent(true);
                break;
            }
            bpPostKey(w, Qt::Key_Down);
            break;
        }
        case Mode::ActionMenu: bpPostKey(m_actionList, Qt::Key_Down); break;
        case Mode::PromptCard: bpPostKey(m_promptList, Qt::Key_Down); break;
    }
}

void BPSettingsOverlay::navLeft()
{
    if (m_mode == Mode::Content) {
        if (isPopupOpen()) return;  // Left/Right don't navigate a combo popup
        if (auto* w = contentFocusWidget()) {
            if (auto* rb = qobject_cast<QRadioButton*>(w))
                cycleRadioButton(rb, -1);
            // Item views own ↑↓ for row navigation, which traps focus on pages
            // that pair the list with form fields (Servers: address/name edits).
            // ←→ is meaningless in that flat list — use it to reach the fields.
            // Other pages keep native ←→ (screenshot grid, tree expansion).
            else if (qobject_cast<QAbstractItemView*>(w) && currentPageCategory() == PageCategory::Servers)
                focusNextInContent(false);
            else
                bpPostKey(w, Qt::Key_Left);
        }
    }
    // TabBar: nothing (sidebar is the leftmost element)
    // ActionMenu: ignore
}

void BPSettingsOverlay::navRight()
{
    switch (m_mode) {
        case Mode::TabBar:    enterContent(); break;
        case Mode::Content:
            if (isPopupOpen()) break;  // Left/Right don't navigate a combo popup
            if (auto* w = contentFocusWidget()) {
                if (auto* rb = qobject_cast<QRadioButton*>(w))
                    cycleRadioButton(rb, +1);
                else if (qobject_cast<QAbstractItemView*>(w) && currentPageCategory() == PageCategory::Servers)
                    focusNextInContent(true);  // see navLeft
                else
                    bpPostKey(w, Qt::Key_Right);
            }
            break;
        case Mode::ActionMenu: break;  // ignore
    }
}

void BPSettingsOverlay::enterContent()
{
    if (m_helpShowing) return;  // help is text-only, no content to enter
    setMode(Mode::Content);
    focusPageContent();
}

void BPSettingsOverlay::exitToTabBar()
{
    // When a combo popup is open, B closes it and stays in Content mode.
    // The user presses B again to return to the tab bar.
    if (isPopupOpen()) {
        if (auto* fw = QApplication::focusWidget())
            bpPostKey(fw, Qt::Key_Escape);
        return;
    }
    // B while editing a value: leave edit mode, stay on the field.
    if (m_editWidget) {
        leaveValueEdit();
        return;
    }
    setMode(Mode::TabBar);
}

void BPSettingsOverlay::doConfirm()
{
    // ExternalResource pages: Return triggers itemActivated → toggle,
    // which duplicates the Start button. Skip here; Start is the toggle.
    if (currentPageCategory() == PageCategory::ExternalResource) return;
    // When a combo popup is open, A confirms the highlighted item.
    if (isPopupOpen()) {
        if (auto* fw = QApplication::focusWidget())
            bpPostKey(fw, Qt::Key_Return);
        return;
    }
    QWidget* fw = QApplication::focusWidget();
    if (!fw || !isAncestorOf(fw)) fw = this;

    // Spinboxes and text fields: A toggles edit mode. Only while editing do their
    // native keys apply; otherwise Up/Down move focus (see isFormWidget / navUp/Down).
    if (needsEditMode(fw)) {
        if (m_editWidget == fw)
            leaveValueEdit();
        else
            enterValueEdit(fw);
        return;
    }

    // Radio buttons: show a popup menu listing all options in the group.
    // Use QButtonGroup (the mutually-exclusive group) when available so we only show
    // buttons from the same group — a parent QWidget may contain multiple radio groups.
    if (auto* rb = qobject_cast<QRadioButton*>(fw)) {
        QList<QAbstractButton*> groupButtons;
        if (auto* bg = rb->group()) {
            groupButtons = bg->buttons();
        } else {
            // No explicit QButtonGroup — fall back to direct siblings in same parent.
            if (QWidget* parent = rb->parentWidget()) {
                for (auto* s : parent->findChildren<QRadioButton*>(QString(), Qt::FindDirectChildrenOnly)) {
                    if (!s->isHidden() && s->isEnabled()) groupButtons.append(s);
                }
            }
        }
        QMenu menu(this);
        for (auto* btn : groupButtons) {
            QAction* action = menu.addAction(btn->text());
            action->setCheckable(true);
            action->setChecked(btn->isChecked());
            connect(action, &QAction::triggered, btn, [btn]() { btn->click(); });
        }
        if (!menu.isEmpty())
            menu.exec(fw->mapToGlobal(fw->rect().bottomLeft()));
        return;
    }

    // QAbstractButton only toggles/clicks on Key_Space (ignores Key_Return).
    // QComboBox opens its popup on Key_Space (Key_Return does nothing).
    // Checkable QGroupBox also responds to Key_Space (it is NOT a QAbstractButton subclass).
    Qt::Key key;
    auto* gb = qobject_cast<QGroupBox*>(fw);
    if (qobject_cast<QAbstractButton*>(fw) || qobject_cast<QComboBox*>(fw) || (gb && gb->isCheckable()))
        key = Qt::Key_Space;
    else
        key = Qt::Key_Return;
    bpPostKey(fw, key);
}

void BPSettingsOverlay::doTabKey()
{
    focusNextInContent(true);
}

void BPSettingsOverlay::sendKeyToFocused(Qt::Key key)
{
    QWidget* fw = QApplication::focusWidget();
    if (!fw || !isAncestorOf(fw)) fw = this;
    bpPostKey(fw, key);
}

void BPSettingsOverlay::triggerPrimaryAction()
{
    switch (currentPageCategory()) {
        case PageCategory::ExternalResource:
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

void BPSettingsOverlay::triggerToggleAction()
{
    if (auto* view = qobject_cast<QAbstractItemView*>(QApplication::focusWidget()))
        bpEnsureCurrentRow(view);
    switch (currentPageCategory()) {
        case PageCategory::ExternalResource:
            sendKeyToFocused(Qt::Key_Space);
            break;
        case PageCategory::Servers:
            triggerPageAction("actionJoin");
            break;
        default:
            break;
    }
}

// ── Action popup ──────────────────────────────────────────────────────────────

void BPSettingsOverlay::buildActionPopup()
{
    m_actionCard = new QWidget(this);
    m_actionCard->hide();
    m_actionCard->setAutoFillBackground(true);

    m_actionTitle = new QLabel(tr("Actions"), m_actionCard);
    {
        QFont f = m_actionTitle->font();
        f.setPixelSize(15);
        f.setBold(true);
        m_actionTitle->setFont(f);
        m_actionTitle->setAlignment(Qt::AlignCenter);
    }

    m_actionList = new QListWidget(m_actionCard);
    m_actionList->setFocusPolicy(Qt::StrongFocus);
    m_actionList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_actionList->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_actionList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_actionList->setIconSize(QSize(22, 22));
    {
        QFont f = m_actionList->font();
        f.setPixelSize(15);
        m_actionList->setFont(f);
    }

    m_actionHud = new QLabel(bpHudHtml(tr("[↑↓] Navigate    [A] Select    [B] Cancel")), m_actionCard);
    m_actionHud->setTextFormat(Qt::RichText);
    m_actionHud->setAlignment(Qt::AlignCenter);
    {
        QFont f = m_actionHud->font();
        f.setPixelSize(13);
        m_actionHud->setFont(f);
    }

    // Mouse support: clicking a row selects it, so activate on click too.
    connect(m_actionList, &QListWidget::itemClicked, this, [this](QListWidgetItem*) { confirmActionMenu(); });

    auto* layout = new QVBoxLayout(m_actionCard);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_actionTitle);
    layout->addWidget(m_actionList, 1);
    layout->addWidget(m_actionHud);
}

QList<QAction*> BPSettingsOverlay::gatherPageActions() const
{
    QList<QAction*> result;
    auto* c = currentContainer();
    if (!c) return result;
    auto* page = dynamic_cast<QWidget*>(c->selectedPage());
    if (!page) return result;

    // WideBar wraps each real QAction in a QWidgetAction placeholder for the toolbar button.
    // toolbar->actions() returns those placeholders (empty text, non-triggerable).
    // We walk findChildren<QAction*>() on the page instead, skipping QWidgetAction wrappers.
    //
    // Actions that have a dedicated controller shortcut are omitted here to avoid duplication:
    // - actionEnableItem / actionDisableItem: handled by Start button (toggle)
    static const QSet<QString> kShortcutActions = { "actionEnableItem", "actionDisableItem" };

    QSet<QAction*> seen;
    QSet<QString>  seenText;  // deduplicate identical labels (e.g. submenu copies)
    for (auto* action : page->findChildren<QAction*>()) {
        if (qobject_cast<QWidgetAction*>(action)) continue;  // skip WideBar placeholders
        if (action->isSeparator()) continue;
        if (action->menu()) continue;  // submenu holders (e.g. "Actions") aren't triggerable
        if (kShortcutActions.contains(action->objectName())) continue;
        const QString label = action->text().remove(u'&').trimmed();
        if (label.isEmpty()) continue;
        if (seen.contains(action))   continue;
        if (seenText.contains(label)) continue;
        seen.insert(action);
        seenText.insert(label);
        result << action;
    }
    return result;
}

void BPSettingsOverlay::positionActionPopup()
{
    if (!m_actionCard) return;
    const int contentX = SIDEBAR_W + 1;
    const int contentW = width() - contentX;
    const int contentH = height() - TITLE_H - HUD_H;

    const int cardW  = qMin(460, contentW - 80);
    const int itemH  = 44;
    const int maxItems = 10;
    const int titleH = 44;
    const int hudH   = 36;
    const int items  = qMin(m_currentActions.size(), maxItems);
    const int cardH  = qMin(titleH + items * itemH + hudH, contentH - 60);

    const int cx = contentX + (contentW - cardW) / 2;
    const int cy = TITLE_H  + (contentH - cardH) / 2;
    m_actionCard->setGeometry(cx, cy, cardW, cardH);

    // Item row heights
    for (int i = 0; i < m_actionList->count(); ++i) {
        if (auto* it = m_actionList->item(i))
            it->setSizeHint(QSize(cardW, itemH));
    }

    m_actionTitle->setFixedHeight(titleH);
    m_actionHud->setFixedHeight(hudH);
}

void BPSettingsOverlay::showActionMenu()
{
    if (m_helpShowing) return;
    // Make sure a row is selected before reading the actions' enabled state —
    // row-dependent ones (remove, edit…) are disabled without a selection.
    if (auto* view = qobject_cast<QAbstractItemView*>(QApplication::focusWidget()))
        bpEnsureCurrentRow(view);
    m_currentActions = gatherPageActions();
    if (m_currentActions.isEmpty()) return;

    m_actionList->clear();
    for (auto* action : m_currentActions) {
        const QString label = action->text().remove(u'&').trimmed();
        auto* item = new QListWidgetItem(action->icon(), label, m_actionList);
        if (!action->isEnabled())
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
    }
    m_actionList->setCurrentRow(0);

    positionActionPopup();
    m_modalScrim->setGeometry(rect());
    m_modalScrim->show();
    m_modalScrim->raise();
    m_actionCard->show();
    m_actionCard->raise();
    bpPopIn(m_actionCard);
    m_actionList->setFocus();
    setMode(Mode::ActionMenu);
}

void BPSettingsOverlay::confirmActionMenu()
{
    const int row = m_actionList->currentRow();
    // Close the menu *before* running the action (standard menu behavior). This
    // also means a confirmation prompt raised by the action is the only card on
    // screen instead of stacking on top of the menu.
    dismissActionMenu();
    if (row >= 0 && row < m_currentActions.size() && m_currentActions[row]->isEnabled())
        m_currentActions[row]->trigger();
}

void BPSettingsOverlay::dismissActionMenu()
{
    m_actionCard->hide();
    if (m_modalScrim && !m_promptCard->isVisible())
        m_modalScrim->hide();
    m_mode = Mode::Content;
    focusPageContent();
    updateHud();
    updateRingTimer();
}

// ── Prompt card (IBigPicturePrompt) ──────────────────────────────────────────

void BPSettingsOverlay::buildPromptCard()
{
    m_promptCard = new QWidget(this);
    m_promptCard->hide();
    m_promptCard->setAutoFillBackground(true);

    m_promptTitle = new QLabel(m_promptCard);
    {
        QFont f = m_promptTitle->font();
        f.setPixelSize(15);
        f.setBold(true);
        m_promptTitle->setFont(f);
        m_promptTitle->setAlignment(Qt::AlignCenter);
    }

    m_promptMessage = new QLabel(m_promptCard);
    m_promptMessage->setWordWrap(true);
    m_promptMessage->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    {
        QFont f = m_promptMessage->font();
        f.setPixelSize(14);
        m_promptMessage->setFont(f);
    }

    m_promptList = new QListWidget(m_promptCard);
    m_promptList->setFocusPolicy(Qt::StrongFocus);
    m_promptList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_promptList->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_promptList->setSelectionMode(QAbstractItemView::SingleSelection);
    {
        QFont f = m_promptList->font();
        f.setPixelSize(14);
        m_promptList->setFont(f);
    }

    m_promptHud = new QLabel(bpHudHtml(tr("[↑↓] Navigate    [A] Select    [B] Cancel")), m_promptCard);
    m_promptHud->setTextFormat(Qt::RichText);
    m_promptHud->setAlignment(Qt::AlignCenter);
    {
        QFont f = m_promptHud->font();
        f.setPixelSize(13);
        m_promptHud->setFont(f);
    }

    // Mouse support: a click both selects and confirms the option.
    connect(m_promptList, &QListWidget::itemClicked, this, [this](QListWidgetItem*) { confirmPrompt(); });

    auto* layout = new QVBoxLayout(m_promptCard);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_promptTitle);
    layout->addWidget(m_promptMessage);
    layout->addWidget(m_promptList, 1);
    layout->addWidget(m_promptHud);
}

void BPSettingsOverlay::positionPromptCard()
{
    if (!m_promptCard) return;
    const int contentX = SIDEBAR_W + 1;
    const int contentW = width() - contentX;
    const int contentH = height() - TITLE_H - HUD_H;

    const int cardW   = qMin(540, contentW - 80);
    const int titleH  = 44;
    const int hudH    = 36;
    const int itemH   = 40;
    const int msgH    = qMax(80, m_promptMessage->heightForWidth(cardW - 32) + 24);
    const int items   = m_promptList->count();
    const int cardH   = qMin(titleH + msgH + items * itemH + hudH, contentH - 60);

    const int cx = contentX + (contentW - cardW) / 2;
    const int cy = TITLE_H  + (contentH - cardH) / 2;
    m_promptCard->setGeometry(cx, cy, cardW, cardH);

    for (int i = 0; i < m_promptList->count(); ++i)
        if (auto* it = m_promptList->item(i))
            it->setSizeHint(QSize(cardW, itemH));

    m_promptTitle->setFixedHeight(titleH);
    m_promptHud->setFixedHeight(hudH);
    m_promptMessage->setContentsMargins(16, 12, 16, 12);
}

int BPSettingsOverlay::execPrompt(const QString& title, const QString& msg, const QStringList& buttons, int defaultIndex)
{
    // Never stack cards: if the action menu is somehow still open, the prompt
    // replaces it and brings it back once answered.
    const bool restoreActionMenu = m_actionCard && m_actionCard->isVisible();
    if (restoreActionMenu)
        m_actionCard->hide();

    m_promptTitle->setText(title);
    m_promptMessage->setText(msg);
    m_promptList->clear();
    for (const auto& btn : buttons)
        m_promptList->addItem(btn);
    m_promptList->setCurrentRow(qBound(0, defaultIndex, m_promptList->count() - 1));
    m_promptResult = -1;

    positionPromptCard();
    m_modalScrim->setGeometry(rect());
    m_modalScrim->show();
    m_modalScrim->raise();
    m_promptCard->show();
    m_promptCard->raise();
    bpPopIn(m_promptCard);
    m_promptList->setFocus();

    const Mode prevMode = m_mode;
    m_mode = Mode::PromptCard;
    updateHud();
    updateRingTimer();

    // Save/restore any outer prompt loop so a nested prompt can't orphan it
    // (an orphaned loop never quits and freezes the launcher).
    QEventLoop* prevLoop = m_promptLoop;
    QEventLoop loop;
    m_promptLoop = &loop;
    loop.exec();
    m_promptLoop = prevLoop;

    m_promptCard->hide();
    if (restoreActionMenu) {
        m_actionCard->show();
        m_actionCard->raise();
        m_actionList->setFocus();
    } else if (m_modalScrim && !m_actionCard->isVisible()) {
        m_modalScrim->hide();
    }
    m_mode = prevMode;
    updateHud();
    updateRingTimer();

    return m_promptResult;
}

void BPSettingsOverlay::confirmPrompt()
{
    m_promptResult = m_promptList->currentRow();
    if (m_promptLoop) m_promptLoop->quit();
}

void BPSettingsOverlay::cancelPrompt()
{
    m_promptResult = -1;
    if (m_promptLoop) m_promptLoop->quit();
}

// ── Help widget ───────────────────────────────────────────────────────────────

void BPSettingsOverlay::buildHelpWidget()
{
    auto* label = new QLabel();
    label->setWordWrap(true);
    label->setTextFormat(Qt::RichText);
    label->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    label->setMargin(24);
    label->setText(helpHtml());
    {
        QFont f = label->font();
        f.setPixelSize(14);
        label->setFont(f);
    }

    m_helpWidget = new QScrollArea(this);
    m_helpWidget->setWidget(label);
    m_helpWidget->setWidgetResizable(true);
    m_helpWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_helpWidget->setFrameShape(QFrame::NoFrame);
    m_helpWidget->hide();
}

QString BPSettingsOverlay::helpHtml() const
{
    return tr(
        "<h2 style='margin-bottom:4px'>Controller Guide</h2>"
        "<hr>"

        "<h3>Main Screen</h3>"
        "<table cellspacing='6'>"
        "<tr><td><b>[A]</b></td><td>Launch selected instance</td></tr>"
        "<tr><td><b>[X]</b></td><td>Open instance options menu</td></tr>"
        "<tr><td><b>[Y]</b></td><td>Open instance settings</td></tr>"
        "<tr><td><b>[Start]</b></td><td>Add a new instance</td></tr>"
        "<tr><td><b>[LB / RB]</b></td><td>Switch group</td></tr>"
        "<tr><td><b>[↑↓←→]</b></td><td>Navigate instances (right stick: fast scroll)</td></tr>"
        "<tr><td><b>[LT / RT]</b></td><td>Page up / down in lists</td></tr>"
        "<tr><td><b>[Guide]</b></td><td>Return to the instance grid from anywhere</td></tr>"
        "</table>"

        "<h3>On-Screen Keyboard (text fields, mod search)</h3>"
        "<table cellspacing='6'>"
        "<tr><td><b>[↑↓←→]</b></td><td>Move between keys</td></tr>"
        "<tr><td><b>[A]</b></td><td>Type the highlighted key</td></tr>"
        "<tr><td><b>[X]</b></td><td>Backspace</td></tr>"
        "<tr><td><b>[Y]</b></td><td>Space</td></tr>"
        "<tr><td><b>[LB / RB]</b></td><td>Move the text cursor</td></tr>"
        "<tr><td><b>[Start]</b></td><td>Done (confirm / search)</td></tr>"
        "<tr><td><b>[B]</b></td><td>Close keyboard</td></tr>"
        "</table>"

        "<h3>Settings – Page List (sidebar)</h3>"
        "<table cellspacing='6'>"
        "<tr><td><b>[↑↓]</b></td><td>Navigate pages</td></tr>"
        "<tr><td><b>[→] or [A]</b></td><td>Enter selected page</td></tr>"
        "<tr><td><b>[LB / RB]</b></td><td>Previous / Next page</td></tr>"
        "<tr><td><b>[B]</b></td><td>Close settings</td></tr>"
        "</table>"

        "<h3>Settings – Page Content</h3>"
        "<table cellspacing='6'>"
        "<tr><td><b>[↑↓ / ←→]</b></td><td>Navigate items</td></tr>"
        "<tr><td><b>[A]</b></td><td>Confirm / Select · on a number field: start editing (↑↓ adjust, A/B finish)</td></tr>"
        "<tr><td><b>[X]</b></td><td>Open action menu (all available actions)</td></tr>"
        "<tr><td><b>[Y]</b></td><td>Download / Add / Install (quick shortcut)</td></tr>"
        "<tr><td><b>[Start]</b></td><td>Toggle enable/disable (mods) · Join (servers)</td></tr>"
        "<tr><td><b>[LB / RB]</b></td><td>Switch tab on tabbed pages · otherwise switch page</td></tr>"
        "<tr><td><b>[B]</b></td><td>Back to page list</td></tr>"
        "</table>"

        "<h3>Action Menu (opened with X)</h3>"
        "<table cellspacing='6'>"
        "<tr><td><b>[↑↓]</b></td><td>Navigate actions</td></tr>"
        "<tr><td><b>[A]</b></td><td>Execute selected action</td></tr>"
        "<tr><td><b>[B]</b></td><td>Cancel / Close menu</td></tr>"
        "</table>"

        "<h3>Options Menu (main screen X)</h3>"
        "<table cellspacing='6'>"
        "<tr><td><b>[↑↓]</b></td><td>Navigate options</td></tr>"
        "<tr><td><b>[A]</b></td><td>Select option</td></tr>"
        "<tr><td><b>[B]</b></td><td>Cancel</td></tr>"
        "</table>"
    );
}

void BPSettingsOverlay::showHelp(bool show)
{
    m_helpShowing = show;
    if (m_helpWidget) m_helpWidget->setVisible(show);
    if (show) {
        if (m_container)       m_container->hide();
        if (m_globalContainer) m_globalContainer->hide();
        updateTitle();
    } else {
        switchContainerForPage(m_currentTab);
    }
    updateHud();
    updateRingTimer();
}

// ── Page category helpers ─────────────────────────────────────────────────────

BPSettingsOverlay::PageCategory BPSettingsOverlay::currentPageCategory() const
{
    auto* c = currentContainer();
    if (!c) return PageCategory::Other;
    auto* page = c->selectedPage();
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
    auto* c = currentContainer();
    if (!c) return false;
    auto* page = dynamic_cast<QWidget*>(c->selectedPage());
    if (!page) return false;
    if (auto* action = page->findChild<QAction*>(actionName)) {
        if (action->isEnabled()) {
            action->trigger();
            return true;
        }
    }
    return false;
}

// ── Private implementation ────────────────────────────────────────────────────

// Radio-button groups are hostile to controllers (only the checked button sits in
// the Tab chain). Replace each group with a QComboBox that proxies clicks to the
// hidden radios, so the pages' load/apply logic keeps working unchanged.
static void convertRadioGroupsToCombos(QWidget* page)
{
    QSet<const QButtonGroup*> seenGroups;
    QSet<const QWidget*> seenParents;
    const auto allRadios = page->findChildren<QRadioButton*>();
    for (auto* rb : allRadios) {
        if (rb->isHidden()) continue;

        // Collect the full mutually-exclusive group this radio belongs to.
        QList<QRadioButton*> group;
        if (auto* bg = rb->group()) {
            if (seenGroups.contains(bg)) continue;
            seenGroups.insert(bg);
            for (auto* btn : bg->buttons())
                if (auto* r = qobject_cast<QRadioButton*>(btn); r && !r->isHidden())
                    group << r;
        } else {
            QWidget* parent = rb->parentWidget();
            if (!parent || seenParents.contains(parent)) continue;
            seenParents.insert(parent);
            for (auto* r : parent->findChildren<QRadioButton*>(QString(), Qt::FindDirectChildrenOnly))
                if (!r->isHidden() && !r->group())
                    group << r;
        }
        if (group.size() < 2) continue;

        QWidget* parent = group.first()->parentWidget();
        QLayout* layout = parent ? parent->layout() : nullptr;
        if (!layout) continue;

        auto* combo = new QComboBox(parent);
        int checked = 0;
        for (int i = 0; i < group.size(); ++i) {
            combo->addItem(group[i]->text().remove(u'&').trimmed());
            if (group[i]->isChecked()) checked = i;
        }
        combo->setCurrentIndex(checked);  // before connect — must not click anything yet

        QLayoutItem* old = layout->replaceWidget(group.first(), combo, Qt::FindChildrenRecursively);
        if (!old) {  // first radio not reachable from this layout — leave the group alone
            delete combo;
            continue;
        }
        delete old;
        for (auto* r : group)
            r->hide();

        QObject::connect(combo, &QComboBox::currentIndexChanged, combo, [group](int idx) {
            if (idx >= 0 && idx < group.size() && !group[idx]->isChecked())
                group[idx]->click();
        });
        // Pages set radios programmatically in opened()/loadSettings — mirror that back.
        for (int i = 0; i < group.size(); ++i) {
            QObject::connect(group[i], &QRadioButton::toggled, combo, [combo, i](bool on) {
                if (on && combo->currentIndex() != i) {
                    QSignalBlocker block(combo);
                    combo->setCurrentIndex(i);
                }
            });
        }
    }
}

void BPSettingsOverlay::rebuild(BaseInstance* instance)
{
    m_instance = instance;

    // ── Instance pages ────────────────────────────────────────────────────────
    m_provider  = std::make_shared<InstancePageProvider>(instance);
    m_container = new PageContainer(m_provider.get(), "settings", this);
    m_container->hidePageList();
    m_container->setBigPictureMode(true);
    m_container->setStyleSheet(bpBigScreenFormStyle());

    m_filteredPages.clear();
    m_instancePageCount = 0;
    for (auto* page : m_container->getPages()) {
        if (!page->shouldDisplay()) continue;
        if (kHiddenPageIds.contains(page->id())) continue;
        m_filteredPages << page;
        m_instancePageCount++;
    }

    // ── Global launcher settings pages ───────────────────────────────────────
    if (auto* globalProvider = APPLICATION->globalSettingsProvider()) {
        m_globalContainer = new PageContainer(globalProvider, "", this);
        m_globalContainer->hidePageList();
        m_globalContainer->setBigPictureMode(true);
        m_globalContainer->setStyleSheet(bpBigScreenFormStyle());
        m_globalContainer->hide();
        for (auto* page : m_globalContainer->getPages()) {
            if (!page->shouldDisplay()) continue;
            m_filteredPages << page;
        }
    }

    // ── Populate sidebar ──────────────────────────────────────────────────────
    // Qt::UserRole stores the page index into m_filteredPages (>=0),
    // HEADER_ROLE (-1) for non-selectable headers/separators,
    // HELP_ROW_ROLE (-2) for the Help entry.

    auto addSeparator = [this](int height = 1) {
        auto* sep = new QListWidgetItem(m_sidebar);
        sep->setFlags(Qt::NoItemFlags);
        sep->setData(Qt::UserRole, HEADER_ROLE);
        sep->setSizeHint(QSize(SIDEBAR_W, height));
    };

    m_sidebar->blockSignals(true);
    m_sidebar->clear();

    // Instance pages
    for (int i = 0; i < m_instancePageCount; ++i) {
        auto* item = new QListWidgetItem(m_filteredPages[i]->icon(), m_filteredPages[i]->displayName(), m_sidebar);
        item->setData(Qt::UserRole, i);
        item->setSizeHint(QSize(SIDEBAR_W, 46));
    }

    // Global pages section
    if (m_globalContainer && m_filteredPages.size() > m_instancePageCount) {
        addSeparator(8);
        // Section header
        auto* header = new QListWidgetItem(tr("Launcher Settings"), m_sidebar);
        header->setData(Qt::UserRole, HEADER_ROLE);
        header->setFlags(Qt::NoItemFlags);
        header->setSizeHint(QSize(SIDEBAR_W, 28));

        for (int i = m_instancePageCount; i < m_filteredPages.size(); ++i) {
            auto* item = new QListWidgetItem(m_filteredPages[i]->icon(), m_filteredPages[i]->displayName(), m_sidebar);
            item->setData(Qt::UserRole, i);
            item->setSizeHint(QSize(SIDEBAR_W, 42));
        }
    }

    // Separator + Help entry
    addSeparator(8);
    auto* help = new QListWidgetItem(tr("? Controller Help"), m_sidebar);
    help->setData(Qt::UserRole, HELP_ROW_ROLE);
    help->setSizeHint(QSize(SIDEBAR_W, 46));

    m_sidebar->blockSignals(false);

    // ── Sync starting tab ─────────────────────────────────────────────────────
    m_currentTab = 0;
    if (auto* cur = m_container->selectedPage()) {
        for (int i = 0; i < m_instancePageCount; ++i) {
            if (m_filteredPages[i] == cur) { m_currentTab = i; break; }
        }
    }
    // Select the matching sidebar row (row may differ from page index due to headers)
    for (int row = 0; row < m_sidebar->count(); ++row) {
        if (auto* it = m_sidebar->item(row); it && it->data(Qt::UserRole).toInt() == m_currentTab) {
            m_sidebar->setCurrentRow(row);
            break;
        }
    }

    // ── Sidebar changed → switch page ─────────────────────────────────────────
    connect(m_sidebar, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0) return;
        auto* item = m_sidebar->item(row);
        if (!item) return;
        const int pageIdx = item->data(Qt::UserRole).toInt();
        if (pageIdx == HELP_ROW_ROLE) {
            showHelp(true);
            return;
        }
        if (pageIdx < 0) return;  // header or separator
        showHelp(false);
        m_currentTab = pageIdx;
        switchContainerForPage(pageIdx);
        updateHud();
    });

    // ── Hide toolbars (actions accessible via X popup) ────────────────────────
    auto hidePageToolbars = [](BasePage* page) {
        if (auto* w = dynamic_cast<QWidget*>(page))
            for (auto* tb : w->findChildren<QToolBar*>())
                tb->hide();
    };
    for (auto* page : m_filteredPages) {
        hidePageToolbars(page);
        if (auto* w = dynamic_cast<QWidget*>(page)) {
            convertRadioGroupsToCombos(w);
            // Redundant in Big Picture mode: global settings live in the sidebar,
            // and the button would open the desktop dialog over the overlay.
            if (auto* globalBtn = w->findChild<QWidget*>("openGlobalSettingsButton"))
                globalBtn->hide();
        }
    }

    auto onPageChanged = [this, hidePageToolbars](BasePage*, BasePage* selected) {
        hidePageToolbars(selected);
        updateHud();
    };
    connect(m_container, &PageContainer::selectedPageChanged, this, onPageChanged);
    if (m_globalContainer)
        connect(m_globalContainer, &PageContainer::selectedPageChanged, this, onPageChanged);

    updateTitle();
    applyTheme();
    relayout();
}

PageContainer* BPSettingsOverlay::currentContainer() const
{
    if (m_globalContainer && !m_globalContainer->isHidden())
        return m_globalContainer;
    return m_container;
}

void BPSettingsOverlay::switchContainerForPage(int pageIndex)
{
    const bool isGlobal = m_globalContainer && (pageIndex >= m_instancePageCount);
    if (isGlobal) {
        m_globalSettingsVisited = true;
        if (m_container) m_container->hide();
        m_globalContainer->show();
        m_globalContainer->selectPage(m_filteredPages[pageIndex]->id());
    } else {
        if (m_globalContainer) m_globalContainer->hide();
        if (m_container) {
            m_container->show();
            m_container->selectPage(m_filteredPages[pageIndex]->id());
        }
    }
    updateTitle();
}

void BPSettingsOverlay::teardown()
{
    if (m_promptLoop) {
        m_promptResult = -1;
        m_promptLoop->quit();
    }
    if (m_globalContainer) {
        m_globalContainer->saveAll();
        delete m_globalContainer;
        m_globalContainer = nullptr;
    }
    if (m_container) {
        m_container->prepareToClose();
        delete m_container;
        m_container = nullptr;
    }
    m_sidebar->blockSignals(true);
    m_sidebar->clear();
    m_sidebar->blockSignals(false);
    m_filteredPages.clear();
    m_instancePageCount = 0;
    m_globalSettingsVisited = false;
    m_provider.reset();
    m_instance    = nullptr;
    m_currentTab  = 0;
    m_helpShowing = false;
    m_editWidget.clear();
    m_orderCache.clear();
    m_orderCachePage = nullptr;
    if (m_actionCard) m_actionCard->hide();
    if (m_promptCard) m_promptCard->hide();
    if (m_helpWidget) m_helpWidget->hide();
    if (m_focusRing) m_focusRing->hide();
    if (m_modalScrim) m_modalScrim->hide();
}

void BPSettingsOverlay::switchToTabIndex(int index, bool focusContent)
{
    if (index < 0 || index >= m_filteredPages.size()) return;
    showHelp(false);
    m_currentTab = index;

    // Sidebar row ≠ page index (section headers add extra rows): scan by UserRole
    m_sidebar->blockSignals(true);
    for (int row = 0; row < m_sidebar->count(); ++row) {
        if (auto* it = m_sidebar->item(row); it && it->data(Qt::UserRole).toInt() == index) {
            m_sidebar->setCurrentRow(row);
            break;
        }
    }
    m_sidebar->blockSignals(false);

    switchContainerForPage(index);
    updateHud();

    if (focusContent) {
        if (m_mode != Mode::Content)
            m_mode = Mode::Content;
        focusPageContent();
        updateHud();
    }
}

void BPSettingsOverlay::setMode(Mode m)
{
    m_mode = m;
    if (m == Mode::TabBar || m == Mode::Content) {
        if (m_actionCard) m_actionCard->hide();
        if (m_modalScrim && m_promptCard && !m_promptCard->isVisible())
            m_modalScrim->hide();
    }
    if (m == Mode::TabBar)
        m_sidebar->setFocus();
    else if (m == Mode::Content)
        focusPageContent();
    updateHud();
    updateRingTimer();
    updateFocusRing();
}

// The fallback poll only needs to run while the ring can actually show —
// Content mode with a page (not help) on screen.
void BPSettingsOverlay::updateRingTimer()
{
    const bool want = isVisible() && m_mode == Mode::Content && !m_helpShowing;
    if (want && !m_ringTimer->isActive())
        m_ringTimer->start();
    else if (!want && m_ringTimer->isActive())
        m_ringTimer->stop();
}

void BPSettingsOverlay::enterValueEdit(QWidget* w)
{
    m_editWidget = w;
    if (m_focusRing) {
        m_focusRing->setProperty("editing", true);
        m_focusRing->update();
    }
    // Text widgets get the on-screen keyboard; spinboxes keep plain ↑↓ adjustment.
    if (qobject_cast<QLineEdit*>(w) || qobject_cast<QTextEdit*>(w) || qobject_cast<QPlainTextEdit*>(w)) {
        if (auto* kb = BPVirtualKeyboard::activeInstance()) {
            connect(kb, &BPVirtualKeyboard::closed, this, &BPSettingsOverlay::onKeyboardClosed,
                    Qt::UniqueConnection);
            kb->openFor(w);
        }
    }
    updateHud();
}

void BPSettingsOverlay::leaveValueEdit()
{
    m_editWidget.clear();
    if (m_focusRing) {
        m_focusRing->setProperty("editing", false);
        m_focusRing->update();
    }
    // Focus moved away (or edit mode ended some other way) — the keyboard's
    // target field is stale, close it. Silent: closed() would re-enter here.
    if (auto* kb = BPVirtualKeyboard::activeInstance(); kb && kb->isVisible() && kb->target() && isAncestorOf(kb->target()))
        kb->dismissSilently();
    updateHud();
}

void BPSettingsOverlay::onKeyboardClosed()
{
    if (m_editWidget)
        leaveValueEdit();
}

void BPSettingsOverlay::pageScroll(bool up)
{
    sendKeyToFocused(up ? Qt::Key_PageUp : Qt::Key_PageDown);
}

void BPSettingsOverlay::updateFocusRing()
{
    if (!m_focusRing) return;
    if (m_mode != Mode::Content || m_helpShowing || !isVisible()) {
        m_focusRing->hide();
        return;
    }
    bpPositionFocusRing(m_focusRing, this, currentContainer());
}

void BPSettingsOverlay::updateHud()
{
    if (!m_hudLabel) return;
    QString hint;
    if (m_mode == Mode::ActionMenu || m_mode == Mode::PromptCard) {
        hint = tr("[↑↓] Navigate    [A] Select    [B] Cancel");
    } else if (m_mode == Mode::TabBar) {
        hint = tr("[↑↓] Select Page    [→/A] Enter    [LB/RB] Prev/Next    [B] Close");
    } else if (m_helpShowing) {
        hint = tr("[↑↓] Scroll    [B] Back to Pages");
    } else if (m_editWidget) {
        if (qobject_cast<QAbstractSpinBox*>(m_editWidget.data()))
            hint = tr("[↑↓] Adjust Value    [A/B] Done");
        else
            hint = tr("Editing — use the on-screen keyboard    [Start] Done    [B] Close");
    } else {
        switch (currentPageCategory()) {
            case PageCategory::ExternalResource:
                hint = tr("[↑↓] Navigate    [Start] Toggle    [X] Actions    [Y] Download    [LB/RB] Page    [B] Back");
                break;
            case PageCategory::Version:
                hint = tr("[↑↓] Navigate    [A] Select    [X] Actions    [Y] Install Loader    [LB/RB] Page    [B] Back");
                break;
            case PageCategory::WorldList:
                hint = tr("[↑↓] Navigate    [A] Open    [X] Actions    [Y] Add    [LB/RB] Page    [B] Back");
                break;
            case PageCategory::Servers:
                hint = tr("[↑↓] Navigate    [←→] Edit Fields    [Start] Join    [X] Actions    [Y] Add    [B] Back");
                break;
            case PageCategory::Screenshots:
                hint = tr("[↑↓] Navigate    [A] Open    [X] Actions    [Y] Copy    [LB/RB] Page    [B] Back");
                break;
            case PageCategory::Settings:
                if (isPopupOpen())
                    hint = tr("[↑↓] Select Option    [A] Confirm    [B] Cancel");
                else if (innerTabWidget())
                    hint = tr("[↑↓] Prev/Next Field    [A] Toggle / Edit    [X] Actions    [LB/RB] Switch Tab    [B] Back");
                else
                    hint = tr("[↑↓] Prev/Next Field    [A] Toggle / Edit    [X] Actions    [LB/RB] Page    [B] Back");
                break;
            case PageCategory::Log:
                hint = tr("[↑↓] Scroll    [LB/RB] Page    [B] Back to Pages");
                break;
            default:
                hint = tr("[↑↓/←→] Navigate    [A] Select    [X] Actions    [LB/RB] Page    [B] Back");
                break;
        }
    }
    // This runs on every focus change; skip the regex/HTML/relayout work when
    // nothing changed. Key includes the glyph style so pad hotswaps re-render.
    const QString key = QString::number(int(bpGlyphStyle())) + hint;
    if (key == m_lastHudKey)
        return;
    m_lastHudKey = key;
    m_hudLabel->setText(bpHudHtml(hint));
}

void BPSettingsOverlay::applyTheme()
{
    const QPalette& pal = QApplication::palette();
    const QColor window    = pal.color(QPalette::Window);
    const QColor base      = pal.color(QPalette::Base);
    const QColor hl        = pal.color(QPalette::Highlight);
    const QColor hlText    = pal.color(QPalette::HighlightedText);
    const QColor text      = pal.color(QPalette::WindowText);
    const QColor sidebarBg = base.darker(110);

    m_titleLabel->setStyleSheet(
        QString("QLabel { color: %1; background: transparent; }").arg(text.name()));

    const QColor dimText = bpDimText(pal);
    m_sidebar->setStyleSheet(
        QString(
            "QListWidget { background: %1; border: none; outline: none; }"
            "QListWidget::item { padding: 10px 16px 10px 18px; color: %2; border-left: 4px solid transparent; }"
            "QListWidget::item:selected:active, QListWidget::item:selected:!active"
            "  { background: %3; color: %4; border-left: 4px solid %5; }"
            "QListWidget::item:hover:!selected { background: %6; }"
            "QListWidget::item:disabled { color: %7; padding: 4px 16px; font-size: 11px; }"
        )
        .arg(sidebarBg.name(), text.name(), hl.name(), hlText.name(),
             hl.lighter(160).name(), window.name(), dimText.name())
    );

    m_hudLabel->setStyleSheet(
        QString("QLabel { background: %1; border-top: 1px solid %2; color: %3; }")
        .arg(base.darker(115).name(), pal.color(QPalette::Mid).name(), text.name())
    );

    // Action popup styling — rounded card over the modal scrim, sidebar-style
    // accent bar on the selected row.
    if (m_actionCard) {
        const QColor cardBg = base;
        const QColor border = pal.color(QPalette::Mid);
        m_actionCard->setStyleSheet(
            QString("QWidget { background: %1; border: 1px solid %2; border-radius: 10px; }")
            .arg(cardBg.name(), border.name())
        );
        m_actionTitle->setStyleSheet(
            QString("QLabel { color: %1; background: %2; border: none; border-bottom: 1px solid %3; padding: 8px;"
                    "  border-top-left-radius: 10px; border-top-right-radius: 10px;"
                    "  border-bottom-left-radius: 0; border-bottom-right-radius: 0; }")
            .arg(text.name(), sidebarBg.name(), border.name())
        );
        m_actionList->setStyleSheet(
            QString(
                "QListWidget { background: %1; border: none; border-radius: 0; outline: none; }"
                "QListWidget::item { padding: 8px 16px; color: %2; border-left: 4px solid transparent; }"
                "QListWidget::item:selected:active, QListWidget::item:selected:!active"
                "  { background: %3; color: %4; border-left: 4px solid %5; }"
                "QListWidget::item:disabled { color: %6; }"
            )
            .arg(cardBg.name(), text.name(), hl.name(), hlText.name(), hl.lighter(160).name(), dimText.name())
        );
        m_actionHud->setStyleSheet(
            QString("QLabel { color: %1; background: %2; border: none; border-top: 1px solid %3; padding: 6px;"
                    "  border-bottom-left-radius: 10px; border-bottom-right-radius: 10px;"
                    "  border-top-left-radius: 0; border-top-right-radius: 0; }")
            .arg(text.name(), sidebarBg.name(), border.name())
        );
    }

    // Prompt card styling (same design language as the action card)
    if (m_promptCard) {
        const QColor cardBg = base;
        const QColor border = pal.color(QPalette::Mid);
        m_promptCard->setStyleSheet(
            QString("QWidget { background: %1; border: 1px solid %2; border-radius: 10px; }")
            .arg(cardBg.name(), border.name())
        );
        m_promptTitle->setStyleSheet(
            QString("QLabel { color: %1; background: %2; border: none; border-bottom: 1px solid %3; padding: 8px;"
                    "  border-top-left-radius: 10px; border-top-right-radius: 10px;"
                    "  border-bottom-left-radius: 0; border-bottom-right-radius: 0; }")
            .arg(text.name(), sidebarBg.name(), border.name())
        );
        m_promptMessage->setStyleSheet(
            QString("QLabel { color: %1; background: %2; border: none; border-radius: 0; }")
            .arg(text.name(), cardBg.name())
        );
        m_promptList->setStyleSheet(
            QString(
                "QListWidget { background: %1; border: none; border-top: 1px solid %2; border-radius: 0; outline: none; }"
                "QListWidget::item { padding: 8px 16px; color: %3; border-left: 4px solid transparent; }"
                "QListWidget::item:selected:active, QListWidget::item:selected:!active"
                "  { background: %4; color: %5; border-left: 4px solid %6; }"
            )
            .arg(cardBg.name(), border.name(), text.name(), hl.name(), hlText.name(), hl.lighter(160).name())
        );
        m_promptHud->setStyleSheet(
            QString("QLabel { color: %1; background: %2; border: none; border-top: 1px solid %3; padding: 6px;"
                    "  border-bottom-left-radius: 10px; border-bottom-right-radius: 10px;"
                    "  border-top-left-radius: 0; border-top-right-radius: 0; }")
            .arg(text.name(), sidebarBg.name(), border.name())
        );
    }

    // Help widget styling
    if (m_helpWidget) {
        m_helpWidget->setStyleSheet(
            QString("QScrollArea { background: %1; border: none; }"
                    "QLabel { color: %2; background: %1; }")
            .arg(window.name(), text.name())
        );
    }
}

void BPSettingsOverlay::updateTitle()
{
    if (!m_titleLabel) return;
    QString section;
    if (m_helpShowing)
        section = tr("Controller Help");
    else if (m_currentTab >= 0 && m_currentTab < m_filteredPages.size())
        section = m_filteredPages[m_currentTab]->displayName();

    const bool onGlobalPage = !m_helpShowing && m_globalContainer && m_currentTab >= m_instancePageCount;
    const QString root = onGlobalPage ? tr("Launcher Settings") : (m_instance ? m_instance->name() : QString());

    if (section.isEmpty())
        m_titleLabel->setText(root);
    else if (root.isEmpty())
        m_titleLabel->setText(section);
    else
        m_titleLabel->setText(root + QStringLiteral("  ›  ") + section);
}

void BPSettingsOverlay::focusPageContent()
{
    auto* c = currentContainer();
    if (!c) return;
    auto* page = dynamic_cast<QWidget*>(c->selectedPage());
    if (!page) return;
    // Prefer the main item view (mods, versions, worlds…) when the page has one.
    for (auto* view : page->findChildren<QAbstractItemView*>()) {
        if (view->isVisibleTo(page) && view->isEnabled()) {
            view->setFocus(Qt::OtherFocusReason);
            // Land on a row too — with no current row, row-dependent actions
            // (remove, edit…) stay disabled, which strands single-entry lists.
            bpEnsureCurrentRow(view);
            return;
        }
    }
    // Otherwise the topmost widget a controller can actually use.
    const QList<QWidget*> order = orderedContentWidgets();
    if (!order.isEmpty())
        order.first()->setFocus(Qt::OtherFocusReason);
}

void BPSettingsOverlay::relayout()
{
    const int w = width();
    const int h = height();

    m_titleLabel->setGeometry(SIDEBAR_W + 20, 0, w - SIDEBAR_W - 40, TITLE_H);

    const int listH = h - TITLE_H - HUD_H;
    m_sidebar->setGeometry(0, TITLE_H, SIDEBAR_W, listH);

    const int contentX = SIDEBAR_W + 1;
    const int contentW = w - contentX;
    if (m_container)
        m_container->setGeometry(contentX, TITLE_H, contentW, listH);
    if (m_globalContainer)
        m_globalContainer->setGeometry(contentX, TITLE_H, contentW, listH);
    if (m_helpWidget)
        m_helpWidget->setGeometry(contentX, TITLE_H, contentW, listH);

    m_hudLabel->setGeometry(0, h - HUD_H, w, HUD_H);

    if (m_modalScrim && m_modalScrim->isVisible())
        m_modalScrim->setGeometry(rect());
    if (m_actionCard && m_actionCard->isVisible())
        positionActionPopup();
    if (m_promptCard && m_promptCard->isVisible())
        positionPromptCard();
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

    p.fillRect(rect(), pal.color(QPalette::Window));
    p.fillRect(0, 0, width(), TITLE_H, pal.color(QPalette::Base));
    p.fillRect(0, TITLE_H - 1, width(), 1, pal.color(QPalette::Mid));

    const int listH = height() - TITLE_H - HUD_H;
    p.fillRect(0, TITLE_H, SIDEBAR_W, listH, pal.color(QPalette::Base).darker(110));
    p.fillRect(SIDEBAR_W, TITLE_H, 1, listH, pal.color(QPalette::Mid));
}
