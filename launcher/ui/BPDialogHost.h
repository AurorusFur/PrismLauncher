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
class FocusRingWidget;

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

    // Gamepad actions — routed from MainWindow while a hosted dialog is open.
    // Same two-level pattern as the settings overlay: ↑↓ move between fields
    // (an item view counts as one field), A "enters" a list — then ↑↓ navigate
    // its rows, A confirms, B goes back to field navigation. B otherwise closes
    // the dialog.
    void navUp();
    void navDown();
    void confirm();
    void cancel();

    // The widget inside `root` that gamepad keys should go to: the currently
    // focused child if there is one, else the main item view, else the first
    // tab-focusable control, else `root` itself. Used for the hosted dialog's
    // initial focus and by MainWindow's gamepad-to-dialog routing.
    static QWidget* preferredFocusChild(QWidget* root);

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;
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
    void updateHud();
    void navVertical(bool down);
    void focusAdjacentField(bool forward);
    QWidget* dialogFocusWidget() const;  // focused widget inside the top dialog, refocused if lost

    QList<QPointer<QDialog>> m_stack;

    // Item view the user "entered" with A — its rows get native ↑↓ until B backs out.
    QPointer<QWidget> m_enteredView;

    // Re-entrancy guard: layoutDialog() itself generates the Move/Resize events
    // the eventFilter re-layouts on.
    bool m_relayouting = false;

    // Big Picture chrome around the hosted dialog
    QLabel* m_titleLabel = nullptr;
    QLabel* m_hudLabel = nullptr;
    QRect m_cardRect;

    // Controller focus indicator (see ui/BPFocusRing.h)
    FocusRingWidget* m_focusRing = nullptr;
    QTimer* m_ringTimer = nullptr;

    static constexpr int TITLE_H = 52;
    static constexpr int HUD_H = 36;
};
