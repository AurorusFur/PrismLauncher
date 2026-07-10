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

#include <QCoreApplication>
#include <QKeyEvent>
#include <QPalette>
#include <QString>
#include <QWidget>

// Posts a synthetic key press/release pair — how the Big Picture layers translate
// gamepad buttons into widget interactions. Shared by the overlay, the resource
// browser, and MainWindow's routing slots.
inline void bpPostKey(QWidget* target, Qt::Key key)
{
    QCoreApplication::postEvent(target, new QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier));
    QCoreApplication::postEvent(target, new QKeyEvent(QEvent::KeyRelease, key, Qt::NoModifier));
}

// dimText: 70% WindowText + 30% Window — readable but clearly secondary.
inline QColor bpDimText(const QPalette& pal)
{
    const QColor text = pal.color(QPalette::WindowText);
    const QColor window = pal.color(QPalette::Window);
    return QColor((text.red() * 7 + window.red() * 3) / 10, (text.green() * 7 + window.green() * 3) / 10,
                  (text.blue() * 7 + window.blue() * 3) / 10);
}

// Stylesheet applied to embedded page and dialog content in big-screen mode.
// Increases font sizes, control heights, and row heights for comfortable
// couch/TV use. Shared by BPSettingsOverlay (settings pages) and BPDialogHost
// (hosted management dialogs).
inline QString bpBigScreenFormStyle()
{
    return QStringLiteral(
        // Base: readable-from-the-couch text everywhere (labels, editors, item views)
        "QWidget          { font-size: 15px; }"
        // Form controls: generous hit targets
        "QCheckBox        { spacing: 10px; min-height: 30px; }"
        "QCheckBox::indicator { width: 22px; height: 22px; }"
        "QRadioButton     { spacing: 10px; min-height: 30px; }"
        "QRadioButton::indicator { width: 22px; height: 22px; }"
        "QLineEdit        { min-height: 36px; padding: 2px 8px; }"
        "QSpinBox, QDoubleSpinBox { min-height: 36px; }"
        "QComboBox        { min-height: 36px; }"
        "QComboBox QAbstractItemView { font-size: 15px; }"
        "QComboBox QAbstractItemView::item { min-height: 34px; }"
        "QPushButton      { min-height: 40px; padding: 4px 16px; }"
        "QToolButton      { min-height: 36px; padding: 4px 10px; }"
        "QGroupBox        { font-weight: bold; }"
        "QGroupBox QWidget { font-weight: normal; }"  // don't let the bold title cascade into children
        "QTabBar::tab     { padding: 10px 20px; }"
        // Item views (mods, versions, worlds, servers, screenshots): tall selectable rows
        "QTreeView::item, QListView::item, QTableView::item { min-height: 38px; padding: 2px 6px; }"
        "QHeaderView::section { min-height: 34px; padding: 4px 8px; font-weight: bold; }"
        // Scrollbars stay visible from a distance
        "QScrollBar:vertical   { width: 16px; }"
        "QScrollBar:horizontal { height: 16px; }");
}
