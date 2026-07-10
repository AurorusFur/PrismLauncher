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

#include <QPointer>
#include <QVector>
#include <QWidget>

class QLabel;
class QLineEdit;

// On-screen keyboard for Big Picture mode: a bottom-sheet panel driven entirely
// by the gamepad, so text entry (mod search, renaming, notes, settings fields)
// never requires a physical keyboard. Custom-painted key grid; the target widget
// keeps focus and is edited through synthetic key events, so it works with any
// text widget (QLineEdit, QTextEdit, QPlainTextEdit, inline item-view editors)
// and physical typing keeps working alongside.
//
// Controls (routed from MainWindow while visible, above everything else):
// d-pad/left stick moves the key cursor, A types, X backspace, Y space,
// LB/RB move the text cursor, Start commits (Done), B closes.
class BPVirtualKeyboard : public QWidget {
    Q_OBJECT

public:
    explicit BPVirtualKeyboard(QWidget* parent);
    ~BPVirtualKeyboard() override;

    void openFor(QWidget* target);
    void dismissSilently();  // hide without emitting closed() — owner-driven teardown
    void reposition();       // stick to the bottom of the parent
    QWidget* target() const { return m_target; }

    // Registered while Big Picture mode is active, same pattern as BPResourceBrowser.
    static BPVirtualKeyboard* activeInstance() { return s_instance; }
    static void setActiveInstance(BPVirtualKeyboard* kb) { s_instance = kb; }

    // Gamepad actions — routed from MainWindow while visible
    void navUp();
    void navDown();
    void navLeft();
    void navRight();
    void pressKey();     // A
    void cancel();       // B — close, keep text
    void backspace();    // X
    void space();        // Y
    void commit();       // Start — done: emits committed() then closed()
    void cursorLeft();   // LB — move text cursor
    void cursorRight();  // RB

signals:
    void committed();  // Start/Done — "run the search" style confirmation
    void closed();     // emitted on every close (including after committed())

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void changeEvent(QEvent*) override;

private:
    void applyTheme();
    struct Key {
        enum Type { Char, Shift, Space, Backspace, Done };
        QString lower;
        QString upper;
        Type type = Char;
    };

    void activate(const Key& key);
    void closeKeyboard(bool commitText);
    void sendText(const QString& text);  // synthetic key event carrying text
    void updatePreview();
    QRect gridRect() const;             // centered key block inside the panel
    QRect keyRect(int row, int col) const;
    void moveVertical(int delta);

    static inline BPVirtualKeyboard* s_instance = nullptr;

    QPointer<QWidget> m_target;
    QVector<QVector<Key>> m_rows;
    int m_row = 1;
    int m_col = 0;
    bool m_shift = false;  // one-shot: types a single uppercase char, then clears

    QLabel* m_previewLabel = nullptr;  // big copy of the target text with a caret
    QLabel* m_hudLabel = nullptr;

    static constexpr int PREVIEW_H = 44;
    static constexpr int HUD_H = 32;
    static constexpr int KEY_SPACING = 8;
    static constexpr int PANEL_MARGIN = 16;
    static constexpr int MAX_GRID_W = 920;
};
