// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2024 Prism Launcher Contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "BPVirtualKeyboard.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QTextEdit>

#include "ui/BPAnim.h"
#include "ui/BPHud.h"
#include "ui/BPStyle.h"

// Inline editors created by item views (rename-in-view) commit/cancel through
// the delegate on Return/Escape rather than by just losing the keyboard.
static bool isItemViewEditor(QWidget* w)
{
    for (QWidget* p = w ? w->parentWidget() : nullptr; p; p = p->parentWidget()) {
        if (qobject_cast<QAbstractItemView*>(p))
            return true;
    }
    return false;
}

BPVirtualKeyboard::BPVirtualKeyboard(QWidget* parent) : QWidget(parent)
{
    setFocusPolicy(Qt::NoFocus);  // the target line edit must keep focus

    auto ch = [](const QString& c) { return Key{ c, c.toUpper(), Key::Char }; };
    auto sym = [](const QString& c) { return Key{ c, c, Key::Char }; };
    m_rows = {
        { sym("1"), sym("2"), sym("3"), sym("4"), sym("5"), sym("6"), sym("7"), sym("8"), sym("9"), sym("0") },
        { ch("q"), ch("w"), ch("e"), ch("r"), ch("t"), ch("y"), ch("u"), ch("i"), ch("o"), ch("p") },
        { ch("a"), ch("s"), ch("d"), ch("f"), ch("g"), ch("h"), ch("j"), ch("k"), ch("l"), sym("-") },
        { ch("z"), ch("x"), ch("c"), ch("v"), ch("b"), ch("n"), ch("m"), sym("_"), sym("."), sym("'") },
        { Key{ tr("Shift"), tr("Shift"), Key::Shift }, Key{ tr("Space"), tr("Space"), Key::Space },
          Key{ tr("Backspace"), tr("Backspace"), Key::Backspace }, Key{ tr("Done"), tr("Done"), Key::Done } },
    };

    m_previewLabel = new QLabel(this);
    m_previewLabel->setTextFormat(Qt::PlainText);  // user text must never render as HTML
    m_previewLabel->setAlignment(Qt::AlignCenter);
    {
        QFont f = m_previewLabel->font();
        f.setPixelSize(19);
        m_previewLabel->setFont(f);
    }

    m_hudLabel = new QLabel(this);
    m_hudLabel->setTextFormat(Qt::RichText);
    m_hudLabel->setAlignment(Qt::AlignCenter);
    m_hudLabel->setText(
        bpHudHtml(tr("[A] Type    [X] Backspace    [Y] Space    [LB/RB] Move Cursor    [Start] Done    [B] Close")));
    {
        QFont f = m_hudLabel->font();
        f.setPixelSize(13);
        m_hudLabel->setFont(f);
    }

    applyTheme();
    hide();
}

void BPVirtualKeyboard::applyTheme()
{
    const QPalette& pal = QApplication::palette();
    m_previewLabel->setStyleSheet(QString("QLabel { color: %1; background: %2; border: 1px solid %3; border-radius: 8px; }")
                                      .arg(pal.color(QPalette::WindowText).name(), pal.color(QPalette::Base).name(),
                                           pal.color(QPalette::Mid).name()));
    m_hudLabel->setStyleSheet(
        QString("QLabel { color: %1; background: transparent; }").arg(pal.color(QPalette::WindowText).name()));
}

void BPVirtualKeyboard::changeEvent(QEvent* ev)
{
    if (ev->type() == QEvent::PaletteChange)
        applyTheme();
    QWidget::changeEvent(ev);
}

BPVirtualKeyboard::~BPVirtualKeyboard()
{
    if (s_instance == this)
        s_instance = nullptr;
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void BPVirtualKeyboard::openFor(QWidget* target)
{
    if (!target)
        return;
    if (m_target && m_target != target)
        disconnect(m_target, nullptr, this, nullptr);
    m_target = target;
    // Live preview updates for the widget types that expose change signals.
    if (auto* le = qobject_cast<QLineEdit*>(target)) {
        connect(le, &QLineEdit::textChanged, this, &BPVirtualKeyboard::updatePreview, Qt::UniqueConnection);
        connect(le, &QLineEdit::cursorPositionChanged, this, &BPVirtualKeyboard::updatePreview, Qt::UniqueConnection);
    } else if (auto* te = qobject_cast<QTextEdit*>(target)) {
        connect(te, &QTextEdit::textChanged, this, &BPVirtualKeyboard::updatePreview, Qt::UniqueConnection);
    } else if (auto* pte = qobject_cast<QPlainTextEdit*>(target)) {
        connect(pte, &QPlainTextEdit::textChanged, this, &BPVirtualKeyboard::updatePreview, Qt::UniqueConnection);
    }

    m_shift = false;
    m_row = 1;  // home row: q
    m_col = 0;
    reposition();
    updatePreview();
    show();
    raise();
    bpSlideIn(this, height() / 3, 160);
}

void BPVirtualKeyboard::dismissSilently()
{
    hide();
}

void BPVirtualKeyboard::closeKeyboard(bool commitText)
{
    hide();
    if (commitText)
        emit committed();
    emit closed();
}

void BPVirtualKeyboard::reposition()
{
    if (!parentWidget())
        return;
    const int w = parentWidget()->width();
    const int h = qBound(240, parentWidget()->height() * 38 / 100, 360);
    setGeometry(0, parentWidget()->height() - h, w, h);
}

// ── Gamepad actions ───────────────────────────────────────────────────────────

void BPVirtualKeyboard::moveVertical(int delta)
{
    const int oldSize = m_rows[m_row].size();
    m_row = (m_row + delta + m_rows.size()) % m_rows.size();
    // Rows differ in length (10 keys vs the 4-key special row) — keep the cursor
    // in roughly the same horizontal position instead of clamping to the left.
    const int newSize = m_rows[m_row].size();
    if (newSize != oldSize) {
        const double frac = oldSize > 1 ? double(m_col) / (oldSize - 1) : 0.0;
        m_col = qRound(frac * (newSize - 1));
    }
    m_col = qBound(0, m_col, newSize - 1);
    update();
}

void BPVirtualKeyboard::navUp()
{
    moveVertical(-1);
}

void BPVirtualKeyboard::navDown()
{
    moveVertical(+1);
}

void BPVirtualKeyboard::navLeft()
{
    const int n = m_rows[m_row].size();
    m_col = (m_col - 1 + n) % n;
    update();
}

void BPVirtualKeyboard::navRight()
{
    const int n = m_rows[m_row].size();
    m_col = (m_col + 1) % n;
    update();
}

// Editing goes through synthetic key events rather than a QLineEdit-specific
// API, so any focused text widget understands it.
void BPVirtualKeyboard::sendText(const QString& text)
{
    if (!m_target)
        return;
    QCoreApplication::postEvent(m_target, new QKeyEvent(QEvent::KeyPress, 0, Qt::NoModifier, text));
    QCoreApplication::postEvent(m_target, new QKeyEvent(QEvent::KeyRelease, 0, Qt::NoModifier, text));
}

void BPVirtualKeyboard::pressKey()
{
    activate(m_rows[m_row][m_col]);
}

void BPVirtualKeyboard::cancel()
{
    // Cancel an inline rename instead of leaving the editor dangling.
    if (m_target && isItemViewEditor(m_target))
        bpPostKey(m_target, Qt::Key_Escape);
    closeKeyboard(false);
}

void BPVirtualKeyboard::backspace()
{
    if (m_target)
        bpPostKey(m_target, Qt::Key_Backspace);
}

void BPVirtualKeyboard::space()
{
    sendText(QStringLiteral(" "));
}

void BPVirtualKeyboard::commit()
{
    // Commit an inline rename through the delegate. Only for single-line
    // editors — Return would insert a newline in notes-style text edits.
    if (m_target && qobject_cast<QLineEdit*>(m_target.data()) && isItemViewEditor(m_target))
        bpPostKey(m_target, Qt::Key_Return);
    closeKeyboard(true);
}

void BPVirtualKeyboard::cursorLeft()
{
    if (m_target)
        bpPostKey(m_target, Qt::Key_Left);
}

void BPVirtualKeyboard::cursorRight()
{
    if (m_target)
        bpPostKey(m_target, Qt::Key_Right);
}

void BPVirtualKeyboard::activate(const Key& key)
{
    switch (key.type) {
        case Key::Char:
            sendText(m_shift ? key.upper : key.lower);
            if (m_shift) {  // one-shot shift, like phone keyboards
                m_shift = false;
                update();
            }
            break;
        case Key::Shift:
            m_shift = !m_shift;
            update();
            break;
        case Key::Space:
            space();
            break;
        case Key::Backspace:
            backspace();
            break;
        case Key::Done:
            commit();
            break;
    }
}

// ── Presentation ──────────────────────────────────────────────────────────────

void BPVirtualKeyboard::updatePreview()
{
    QString text;
    if (auto* le = qobject_cast<QLineEdit*>(m_target.data())) {
        text = le->text();
        text.insert(qBound(0, le->cursorPosition(), int(text.size())), QChar(u'|'));
    } else if (auto* te = qobject_cast<QTextEdit*>(m_target.data())) {
        text = te->toPlainText();
    } else if (auto* pte = qobject_cast<QPlainTextEdit*>(m_target.data())) {
        text = pte->toPlainText();
    }
    // Multi-line targets: a one-line preview only fits the tail being typed.
    text.replace(QChar(u'\n'), QChar(u' '));
    if (text.size() > 80)
        text = QStringLiteral("…") + text.right(79);
    m_previewLabel->setText(text);
}

QRect BPVirtualKeyboard::gridRect() const
{
    const int top = PANEL_MARGIN + PREVIEW_H + KEY_SPACING;
    const int gh = height() - top - HUD_H - PANEL_MARGIN;
    const int gw = qMin(width() - 2 * PANEL_MARGIN, MAX_GRID_W);
    return QRect((width() - gw) / 2, top, gw, gh);
}

QRect BPVirtualKeyboard::keyRect(int row, int col) const
{
    const QRect grid = gridRect();
    const int rows = m_rows.size();
    const int keyH = (grid.height() - (rows - 1) * KEY_SPACING) / rows;
    const int cols = m_rows[row].size();
    const int keyW = (grid.width() - (cols - 1) * KEY_SPACING) / cols;
    return QRect(grid.x() + col * (keyW + KEY_SPACING), grid.y() + row * (keyH + KEY_SPACING), keyW, keyH);
}

void BPVirtualKeyboard::resizeEvent(QResizeEvent* ev)
{
    QWidget::resizeEvent(ev);
    m_previewLabel->setGeometry(PANEL_MARGIN, PANEL_MARGIN, width() - 2 * PANEL_MARGIN, PREVIEW_H);
    m_hudLabel->setGeometry(0, height() - HUD_H, width(), HUD_H);
}

void BPVirtualKeyboard::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette& pal = QApplication::palette();
    const QColor panelBg = pal.color(QPalette::Base).darker(112);
    const QColor keyBg = pal.color(QPalette::Base);
    const QColor keyBorder = pal.color(QPalette::Mid);
    const QColor text = pal.color(QPalette::WindowText);
    const QColor hl = pal.color(QPalette::Highlight);
    const QColor hlText = pal.color(QPalette::HighlightedText);

    p.fillRect(rect(), panelBg);
    p.fillRect(0, 0, width(), 1, keyBorder);

    QFont keyFont = font();
    keyFont.setPixelSize(17);

    for (int row = 0; row < m_rows.size(); ++row) {
        for (int col = 0; col < m_rows[row].size(); ++col) {
            const Key& key = m_rows[row][col];
            const QRect r = keyRect(row, col);
            const bool current = (row == m_row && col == m_col);
            const bool shiftOn = (key.type == Key::Shift && m_shift);

            p.setPen(QPen(current ? hl : keyBorder, current ? 3 : 1));
            p.setBrush(current ? hl : (shiftOn ? hl.darker(140) : keyBg));
            p.drawRoundedRect(QRectF(r).adjusted(1, 1, -1, -1), 7, 7);

            keyFont.setBold(current || key.type != Key::Char);
            p.setFont(keyFont);
            p.setPen(current ? hlText : (shiftOn ? hlText : text));
            p.drawText(r, Qt::AlignCenter, m_shift ? key.upper : key.lower);
        }
    }
}

void BPVirtualKeyboard::mousePressEvent(QMouseEvent* ev)
{
    for (int row = 0; row < m_rows.size(); ++row) {
        for (int col = 0; col < m_rows[row].size(); ++col) {
            if (keyRect(row, col).contains(ev->pos())) {
                m_row = row;
                m_col = col;
                update();
                activate(m_rows[row][col]);
                return;
            }
        }
    }
    QWidget::mousePressEvent(ev);
}
