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

#include <QApplication>
#include <QPainter>
#include <QPen>
#include <QWidget>

// Console-style focus indicator: a highlight-colored rounded ring drawn on top of
// whatever widget currently has keyboard focus. The native dotted focus rectangle
// is invisible from a couch; this is not. Shared by BPSettingsOverlay (page
// content) and BPDialogHost (hosted dialogs).
class FocusRingWidget : public QWidget {
public:
    explicit FocusRingWidget(QWidget* parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        hide();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        // While a value is being edited (spinbox/text edit mode) the ring turns
        // amber so the user can tell "navigating" apart from "adjusting".
        const QColor hl = property("editing").toBool() ? QColor(0xd8, 0xb9, 0x44)
                                                       : QApplication::palette().color(QPalette::Highlight);
        QColor fill = hl;
        fill.setAlpha(22);
        p.setPen(QPen(hl, 3));
        p.setBrush(fill);
        p.drawRoundedRect(QRectF(rect()).adjusted(2, 2, -2, -2), 6, 6);
    }
};
