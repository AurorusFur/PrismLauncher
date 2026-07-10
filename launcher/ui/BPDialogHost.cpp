// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2024 Prism Launcher Contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "BPDialogHost.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QGroupBox>
#include <QLabel>
#include <QPainter>
#include <QResizeEvent>
#include <QTimer>

#include "ui/BPFocusRing.h"
#include "ui/BPHud.h"
#include "ui/BPNav.h"
#include "ui/BPStyle.h"

BPDialogHost::BPDialogHost(QWidget* parent) : QWidget(parent)
{
    m_titleLabel = new QLabel(this);
    {
        QFont f = m_titleLabel->font();
        f.setPixelSize(17);
        f.setBold(true);
        m_titleLabel->setFont(f);
        m_titleLabel->setAlignment(Qt::AlignCenter);
    }

    m_hudLabel = new QLabel(this);
    m_hudLabel->setTextFormat(Qt::RichText);
    m_hudLabel->setAlignment(Qt::AlignCenter);
    {
        QFont f = m_hudLabel->font();
        f.setPixelSize(13);
        m_hudLabel->setFont(f);
    }
    updateHud();

    m_focusRing = new FocusRingWidget(this);
    // Same approach as the settings overlay: event-driven from focusChanged,
    // with a slow fallback poll for moves that emit no signal (scrolling under
    // a fixed focus widget, model resets, async updates).
    m_ringTimer = new QTimer(this);
    m_ringTimer->setInterval(200);
    connect(m_ringTimer, &QTimer::timeout, this, &BPDialogHost::updateFocusRing);
    connect(qApp, &QApplication::focusChanged, this, [this] {
        if (isVisible())
            updateFocusRing();
    });

    applyTheme();
    hide();
}

BPDialogHost::~BPDialogHost()
{
    // Never let the host delete hosted dialogs — many are stack-allocated by
    // their callers (e.g. `ModDownloadDialog mdownload(this…)`). Hand any
    // survivors back to the windowing system instead.
    for (auto& dlg : m_stack) {
        if (!dlg)
            continue;
        const bool wasVisible = dlg->isVisible();
        dlg->setParent(nullptr);
        dlg->setWindowFlags(Qt::Dialog);
        if (wasVisible)
            dlg->show();
    }
}

void BPDialogHost::hostDialog(QDialog* dialog)
{
    if (!dialog || m_stack.contains(dialog))
        return;

    dialog->setProperty("bpHosted", true);  // checked by the Show hook — prevents re-entry
    dialog->setParent(this);                // strips the window flag → plain child widget
    dialog->setWindowFlags(Qt::Widget);
    dialog->setAutoFillBackground(true);    // child widgets don't paint a window background
    dialog->setStyleSheet(bpBigScreenFormStyle());  // couch-readable controls, like overlay pages
    connect(dialog, &QDialog::finished, this, &BPDialogHost::dialogClosed);
    connect(dialog, &QObject::destroyed, this, &BPDialogHost::purge);
    connect(dialog, &QWidget::windowTitleChanged, this, [this](const QString& title) { m_titleLabel->setText(title); });

    // Only the topmost dialog is interactive/visible.
    if (QDialog* below = activeDialog())
        below->hide();
    m_stack << dialog;
    m_enteredView.clear();
    updateHud();

    if (parentWidget())
        setGeometry(parentWidget()->rect());
    m_titleLabel->setText(dialog->windowTitle());
    layoutDialog(dialog);
    show();
    raise();
    // The dialog may have briefly existed as a native window and taken activation
    // with it — reclaim it so focus and key delivery stay in the main window.
    window()->activateWindow();
    dialog->show();
    dialog->raise();
    focusDialogContent(dialog);
}

// Reparenting bypasses QDialog's normal initial-focus logic, so nothing inside a
// hosted dialog has focus and gamepad keys would hit the dialog frame and die.
// Deferred because at Show-event time the dialog's children aren't laid out yet.
void BPDialogHost::focusDialogContent(QDialog* dialog)
{
    QTimer::singleShot(0, dialog, [dialog] {
        if (QWidget* w = preferredFocusChild(dialog))
            w->setFocus(Qt::OtherFocusReason);
    });
}

QWidget* BPDialogHost::preferredFocusChild(QWidget* root)
{
    if (!root)
        return nullptr;
    if (QWidget* fc = root->focusWidget()) {  // remembered focus child
        if (fc->isVisibleTo(root) && fc->isEnabled())
            return fc;
    }
    for (auto* v : root->findChildren<QAbstractItemView*>()) {
        if (v->isVisibleTo(root) && v->isEnabled())
            return v;
    }
    for (auto* w : root->findChildren<QWidget*>()) {
        if (w->isVisibleTo(root) && w->isEnabled() && (w->focusPolicy() & Qt::TabFocus) && !w->focusProxy())
            return w;
    }
    return root;
}

QDialog* BPDialogHost::activeDialog() const
{
    for (auto it = m_stack.rbegin(); it != m_stack.rend(); ++it) {
        if (*it)
            return *it;
    }
    return nullptr;
}

void BPDialogHost::layoutDialog(QDialog* dialog)
{
    if (!dialog)
        return;
    // Respect the dialog's own size when it set one, fall back to the size hint,
    // and cap so the card (title + dialog + hint bar) uses most of the screen.
    QSize s = dialog->size();
    if (s.width() < 200 || s.height() < 120)
        s = dialog->sizeHint();
    s = s.expandedTo(dialog->minimumSizeHint());
    s = s.boundedTo(QSize(width() * 92 / 100, height() * 92 / 100 - TITLE_H - HUD_H));

    const int cardW = s.width();
    const int cardH = TITLE_H + s.height() + HUD_H;
    const int cx = (width() - cardW) / 2;
    const int cy = (height() - cardH) / 2;
    m_cardRect = QRect(cx, cy, cardW, cardH);

    m_titleLabel->setGeometry(cx, cy, cardW, TITLE_H);
    dialog->setGeometry(cx, cy + TITLE_H, cardW, s.height());
    m_hudLabel->setGeometry(cx, cy + TITLE_H + s.height(), cardW, HUD_H);
    m_titleLabel->raise();
    m_hudLabel->raise();
}

void BPDialogHost::dialogClosed()
{
    auto* dlg = qobject_cast<QDialog*>(sender());
    if (dlg) {
        m_stack.removeAll(QPointer<QDialog>(dlg));
        dlg->hide();
    }
    purge();
}

void BPDialogHost::purge()
{
    m_stack.removeAll(QPointer<QDialog>(nullptr));
    m_enteredView.clear();
    updateHud();
    if (QDialog* top = activeDialog()) {
        m_titleLabel->setText(top->windowTitle());
        layoutDialog(top);
        top->show();
        top->raise();
        focusDialogContent(top);
        m_titleLabel->raise();
        m_hudLabel->raise();
    } else {
        hide();
    }
}

// ── Gamepad navigation (mirrors the settings overlay's content mode) ─────────

QWidget* BPDialogHost::dialogFocusWidget() const
{
    QDialog* top = activeDialog();
    if (!top)
        return nullptr;
    QWidget* fw = QApplication::focusWidget();
    if (fw && top->isAncestorOf(fw))
        return fw;
    fw = preferredFocusChild(top);
    if (fw)
        fw->setFocus(Qt::OtherFocusReason);
    return fw;
}

void BPDialogHost::navUp()
{
    navVertical(false);
}

void BPDialogHost::navDown()
{
    navVertical(true);
}

void BPDialogHost::navVertical(bool down)
{
    const Qt::Key key = down ? Qt::Key_Down : Qt::Key_Up;
    // Combo popup open — focus lives outside the dialog; drive the popup.
    if (QApplication::activePopupWidget()) {
        if (auto* fw = QApplication::focusWidget())
            bpPostKey(fw, key);
        return;
    }
    QWidget* fw = dialogFocusWidget();
    if (!fw)
        return;
    // Inside an entered list, ↑↓ are row navigation; otherwise they move
    // between fields (the list itself counts as a single field).
    if (m_enteredView && fw == m_enteredView)
        bpPostKey(fw, key);
    else
        focusAdjacentField(down);
}

void BPDialogHost::focusAdjacentField(bool forward)
{
    QDialog* top = activeDialog();
    if (!top)
        return;
    const QList<QWidget*> order = bpOrderedControllerWidgets(top, top);
    if (order.isEmpty())
        return;
    QWidget* fw = QApplication::focusWidget();
    int idx = order.indexOf(fw);
    if (idx < 0) {
        order.first()->setFocus(Qt::OtherFocusReason);
        return;
    }
    idx = (idx + (forward ? 1 : -1) + order.size()) % order.size();
    order[idx]->setFocus(forward ? Qt::TabFocusReason : Qt::BacktabFocusReason);
}

void BPDialogHost::confirm()
{
    // A confirms the highlighted popup item.
    if (QApplication::activePopupWidget()) {
        if (auto* fw = QApplication::focusWidget())
            bpPostKey(fw, Qt::Key_Return);
        return;
    }
    QWidget* fw = dialogFocusWidget();
    if (!fw)
        return;
    // Lists take two confirmations: the first A enters the list, the second
    // activates the highlighted row (Return also triggers the dialog's default
    // button, e.g. "Select" in version pickers).
    if (auto* view = qobject_cast<QAbstractItemView*>(fw)) {
        if (m_enteredView != view) {
            m_enteredView = view;
            bpEnsureCurrentRow(view);
            updateHud();
            return;
        }
        bpPostKey(view, Qt::Key_Return);
        return;
    }
    // QAbstractButton/QComboBox/checkable QGroupBox only react to Key_Space.
    auto* gb = qobject_cast<QGroupBox*>(fw);
    const bool wantsSpace = qobject_cast<QAbstractButton*>(fw) || qobject_cast<QComboBox*>(fw) || (gb && gb->isCheckable());
    bpPostKey(fw, wantsSpace ? Qt::Key_Space : Qt::Key_Return);
}

void BPDialogHost::cancel()
{
    // B closes an open combo popup and stays in the dialog.
    if (QApplication::activePopupWidget()) {
        if (auto* fw = QApplication::focusWidget())
            bpPostKey(fw, Qt::Key_Escape);
        return;
    }
    // B inside an entered list backs out to field navigation.
    if (m_enteredView) {
        m_enteredView.clear();
        updateHud();
        return;
    }
    if (QDialog* top = activeDialog())
        bpPostKey(top, Qt::Key_Escape);  // reject/close
}

void BPDialogHost::updateHud()
{
    if (!m_hudLabel)
        return;
    if (m_enteredView)
        m_hudLabel->setText(bpHudHtml(tr("[↑↓] Navigate    [A] Confirm    [LB/RB] Scroll    [B] Back to Fields")));
    else
        m_hudLabel->setText(bpHudHtml(tr("[↑↓] Switch Field    [A] Select / Enter List    [←→] Adjust    [B] Close")));
}

void BPDialogHost::updateFocusRing()
{
    if (!m_focusRing)
        return;
    QDialog* top = activeDialog();
    QWidget* fw = QApplication::focusWidget();
    if (!isVisible() || !top || !fw || !top->isAncestorOf(fw)) {
        m_focusRing->hide();
        return;
    }
    // An *entered* list draws its own row highlight; a list that's merely the
    // current field stop keeps the ring so the user can see where they are.
    // (Focus may sit on the view itself or on its viewport via focus proxy.)
    QWidget* logical = fw;
    if (fw->parentWidget() && qobject_cast<QAbstractItemView*>(fw->parentWidget()))
        logical = fw->parentWidget();
    if (m_enteredView && logical == m_enteredView) {
        m_focusRing->hide();
        return;
    }
    QRect r(logical->mapTo(this, QPoint(0, 0)), logical->size());
    r.adjust(-5, -5, 5, 5);
    r &= top->geometry();
    if (r.width() < 8 || r.height() < 8) {
        m_focusRing->hide();
        return;
    }
    m_focusRing->moveTo(r);
    m_focusRing->show();
    m_focusRing->raise();
}

void BPDialogHost::applyTheme()
{
    const QPalette& pal = QApplication::palette();
    const QColor base = pal.color(QPalette::Base);
    const QColor mid = pal.color(QPalette::Mid);
    const QColor text = pal.color(QPalette::WindowText);
    const QColor headerBg = base.darker(110);

    m_titleLabel->setStyleSheet(
        QString("QLabel { color: %1; background: %2; border: 1px solid %3; border-bottom: none;"
                "  border-top-left-radius: 10px; border-top-right-radius: 10px; }")
            .arg(text.name(), headerBg.name(), mid.name()));
    m_hudLabel->setStyleSheet(
        QString("QLabel { color: %1; background: %2; border: 1px solid %3; border-top: 1px solid %3;"
                "  border-bottom-left-radius: 10px; border-bottom-right-radius: 10px; }")
            .arg(text.name(), headerBg.name(), mid.name()));
}

void BPDialogHost::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, 170));
    if (!m_cardRect.isNull()) {
        // Card frame behind the hosted dialog (title/hint bars draw their own bg)
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QApplication::palette().color(QPalette::Mid), 1));
        p.setBrush(QApplication::palette().color(QPalette::Base));
        p.drawRoundedRect(QRectF(m_cardRect).adjusted(-1.5, -1.5, 1.5, 1.5), 10, 10);
    }
}

void BPDialogHost::resizeEvent(QResizeEvent* ev)
{
    QWidget::resizeEvent(ev);
    if (QDialog* top = activeDialog())
        layoutDialog(top);
}

void BPDialogHost::changeEvent(QEvent* ev)
{
    if (ev->type() == QEvent::PaletteChange)
        applyTheme();
    QWidget::changeEvent(ev);
}

void BPDialogHost::showEvent(QShowEvent* ev)
{
    // Lets CustomMessageBox know its boxes will be hosted here rather than
    // routed to the settings overlay's prompt card (which we would cover).
    qApp->setProperty("bpDialogHostActive", true);
    if (m_ringTimer)
        m_ringTimer->start();
    QWidget::showEvent(ev);
}

void BPDialogHost::hideEvent(QHideEvent* ev)
{
    qApp->setProperty("bpDialogHostActive", false);
    if (m_ringTimer)
        m_ringTimer->stop();
    if (m_focusRing)
        m_focusRing->hide();
    m_enteredView.clear();
    updateHud();
    QWidget::hideEvent(ev);
}
