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
#include <QWidget>

class QPushButton;
class QLabel;

// Fullscreen overlay widget (NOT a dialog) — no exec(), no nested event loop.
// Shown/hidden directly; emits signals when the user picks an action or cancels.
class BPOptionsMenu : public QWidget {
    Q_OBJECT

public:
    enum Action { NoAction = -1, Launch, Settings, Rename, Copy, Delete, ChangeIcon };

    explicit BPOptionsMenu(QWidget* parent = nullptr);

    void setInstanceName(const QString& name);

public slots:
    void navigatePrev();
    void navigateNext();
    void confirmCurrent();
    void dismiss();

signals:
    void actionSelected(BPOptionsMenu::Action action);
    void dismissed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void setHighlight(int index);
    void layoutCard();

    QString m_instanceName;
    int m_current = 0;

    QWidget* m_card = nullptr;
    QLabel* m_title = nullptr;
    QList<QPushButton*> m_buttons;

    static constexpr int CARD_WIDTH   = 520;
    static constexpr int BTN_HEIGHT   = 62;
    static constexpr int BTN_SPACING  = 8;
    static constexpr int CARD_PADDING = 24;
    static constexpr int TITLE_HEIGHT = 48;
};
