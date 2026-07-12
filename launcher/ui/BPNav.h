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

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QLabel>
#include <QList>
#include <QPlainTextEdit>
#include <QTabBar>
#include <QTextEdit>
#include <QWidget>
#include <algorithm>

// D-pad focus traversal helpers shared by BPSettingsOverlay (page content) and
// BPDialogHost (hosted dialogs).

// Returns true if a controller user can meaningfully focus this widget.
// Filters out everything Qt's raw tab chain would visit but a D-pad can't use:
// labels, tab bars (LB/RB switch tabs), plain scroll frames, internal helper
// widgets, and anything invisible. Text fields stay focusable — they use the
// explicit edit mode / on-screen keyboard (A to edit, A/B to finish).
inline bool bpControllerFocusable(QWidget* w, QWidget* scope)
{
    if (!w || !scope || !scope->isAncestorOf(w)) return false;
    if (!w->isVisibleTo(scope) || !w->isEnabled()) return false;
    if (!(w->focusPolicy() & Qt::TabFocus)) return false;
    if (w->focusProxy()) return false;  // spinbox/combo internal line edits, scroll areas
    if (qobject_cast<QLabel*>(w)) return false;
    if (qobject_cast<QTabBar*>(w)) return false;
    if (auto* sa = qobject_cast<QAbstractScrollArea*>(w);
        sa && !qobject_cast<QAbstractItemView*>(w) && !qobject_cast<QTextEdit*>(w) && !qobject_cast<QPlainTextEdit*>(w))
        return false;  // plain QScrollArea frames — focus belongs to their contents
    return true;
}

// All controller-usable widgets under `root` in *visual* order: top-to-bottom,
// then left-to-right within a row. Qt's declared tab order often disagrees with
// the layout (Down jumping left or back to the top), which is disorienting with
// a D-pad — so navigation is driven by geometry instead. `scope` is the widget
// visibility/ancestry are checked against (usually `root` itself).
inline QList<QWidget*> bpOrderedControllerWidgets(QWidget* root, QWidget* scope)
{
    QList<QWidget*> list;
    if (!root || !scope)
        return list;
    for (auto* w : root->findChildren<QWidget*>())
        if (bpControllerFocusable(w, scope))
            list << w;
    std::stable_sort(list.begin(), list.end(), [root](QWidget* a, QWidget* b) {
        const QPoint pa = a->mapTo(root, QPoint(0, 0));
        const QPoint pb = b->mapTo(root, QPoint(0, 0));
        if (qAbs(pa.y() - pb.y()) > 8)  // same-row tolerance
            return pa.y() < pb.y();
        return pa.x() < pb.x();
    });
    return list;
}

// Item views only enable their actions (remove, edit…) once a row is actually
// current *and selected* — make sure focusing one lands on a row, even a lone one.
inline void bpEnsureCurrentRow(QAbstractItemView* view)
{
    if (!view || !view->model() || !view->selectionModel())
        return;
    const QModelIndex cur = view->currentIndex();
    if (!cur.isValid()) {
        if (view->model()->rowCount(view->rootIndex()) > 0)
            view->setCurrentIndex(view->model()->index(0, 0, view->rootIndex()));
        return;
    }
    // QAbstractItemView::focusInEvent gives a freshly focused view a current
    // index with SelectionFlag::NoUpdate — current but not selected. Arrow keys
    // normally fix that on the first cursor move, but a single-row list has
    // nowhere to move, so the row would stay unselected (faint focus tint, no
    // highlight, row-actions disabled) forever. Promote it to a real selection.
    if (view->selectionMode() != QAbstractItemView::NoSelection && !view->selectionModel()->hasSelection())
        view->setCurrentIndex(cur);
}
