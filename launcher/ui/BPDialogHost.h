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
#include <QPointer>
#include <QWidget>

class QDialog;
class QLabel;
class QTimer;

// Hosts QDialogs *inside* MainWindow while Big Picture mode is active, so
// management windows (resource downloader, loader version select, progress
// dialogs…) appear as centered cards over a scrim instead of separate desktop
// windows. Big Picture mode never spawns extra windows this way.
//
// Dialogs are reparented into the host when they are shown (see the QEvent::Show
// hook in MainWindow::eventFilter). Their exec() loops keep working — accept/
// reject exits as usual — but window modality no longer applies, so MainWindow
// routes all gamepad input to the topmost hosted dialog while one is open.
class BPDialogHost : public QWidget {
    Q_OBJECT

public:
    explicit BPDialogHost(QWidget* parent);
    ~BPDialogHost() override;

    void hostDialog(QDialog* dialog);
    QDialog* activeDialog() const;  // topmost hosted dialog, or nullptr

    // The widget inside `root` that gamepad keys should go to: the currently
    // focused child if there is one, else the main item view, else the first
    // tab-focusable control, else `root` itself. Used for the hosted dialog's
    // initial focus and by MainWindow's gamepad-to-dialog routing.
    static QWidget* preferredFocusChild(QWidget* root);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void changeEvent(QEvent*) override;
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;

private:
    void layoutDialog(QDialog* dialog);
    void dialogClosed();
    void purge();  // drop deleted/hidden dialogs, hide host when empty
    void applyTheme();
    void focusDialogContent(QDialog* dialog);  // deferred initial focus
    void updateFocusRing();

    QList<QPointer<QDialog>> m_stack;

    // Big Picture chrome around the hosted dialog
    QLabel* m_titleLabel = nullptr;
    QLabel* m_hudLabel = nullptr;
    QRect m_cardRect;

    // Controller focus indicator (see ui/BPFocusRing.h)
    QWidget* m_focusRing = nullptr;
    QTimer* m_ringTimer = nullptr;

    static constexpr int TITLE_H = 52;
    static constexpr int HUD_H = 36;
};
