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

#include <QRegularExpression>
#include <QString>

// Which controller's glyphs the HUD hints should show. HUD strings are written
// in Xbox terms ([A], [LB/RB], [Start]) and translated to the connected pad's
// labels at render time. Set by MainWindow from GamepadController::padKind().
enum class BPGlyphStyle { Xbox, PlayStation, Nintendo };

inline BPGlyphStyle& bpGlyphStyle()
{
    static BPGlyphStyle style = BPGlyphStyle::Xbox;
    return style;
}

// Formats a plain controller HUD hint like "[A] Select    [B] Cancel" as rich HTML:
// face buttons get their standard controller colors/symbols for the connected pad
// (Xbox: colored A/B/X/Y letters; PlayStation: colored ✕○□△ shapes; Nintendo:
// plain labels), all bracketed tokens are bold, and space runs survive HTML
// whitespace collapsing. Labels showing the result must use Qt::RichText.
inline QString bpHudHtml(QString hint)
{
    hint = hint.toHtmlEscaped();

    static const QRegularExpression tokenRe(QStringLiteral("\\[([^\\]]+)\\]"));
    // Only standalone letters are touched: the B in "LB/RB" has no word boundary before it.
    static const QRegularExpression aRe(QStringLiteral("\\bA\\b"));
    static const QRegularExpression bRe(QStringLiteral("\\bB\\b"));
    static const QRegularExpression xRe(QStringLiteral("\\bX\\b"));
    static const QRegularExpression yRe(QStringLiteral("\\bY\\b"));
    static const QRegularExpression lbRe(QStringLiteral("\\bLB\\b"));
    static const QRegularExpression rbRe(QStringLiteral("\\bRB\\b"));
    static const QRegularExpression startRe(QStringLiteral("\\bStart\\b"));

    const BPGlyphStyle style = bpGlyphStyle();

    QString out;
    qsizetype last = 0;
    auto it = tokenRe.globalMatch(hint);
    while (it.hasNext()) {
        const auto m = it.next();
        out += hint.mid(last, m.capturedStart() - last);
        QString token = m.captured(1);
        switch (style) {
            case BPGlyphStyle::Xbox:
                token.replace(aRe, QStringLiteral("<span style='color:#59b55f'>A</span>"));
                token.replace(bRe, QStringLiteral("<span style='color:#e0605e'>B</span>"));
                token.replace(xRe, QStringLiteral("<span style='color:#5a9bd8'>X</span>"));
                token.replace(yRe, QStringLiteral("<span style='color:#d8b944'>Y</span>"));
                break;
            case BPGlyphStyle::PlayStation:
                // SDL positions: south=A → cross, east=B → circle, west=X → square, north=Y → triangle
                token.replace(lbRe, QStringLiteral("L1"));
                token.replace(rbRe, QStringLiteral("R1"));
                token.replace(startRe, QStringLiteral("Options"));
                token.replace(aRe, QStringLiteral("<span style='color:#7db8e8'>&#10005;</span>"));  // ✕
                token.replace(bRe, QStringLiteral("<span style='color:#e0605e'>&#9675;</span>"));   // ○
                token.replace(xRe, QStringLiteral("<span style='color:#e78ac2'>&#9633;</span>"));   // □
                token.replace(yRe, QStringLiteral("<span style='color:#4fbf9f'>&#9651;</span>"));   // △
                break;
            case BPGlyphStyle::Nintendo:
                // SDL uses Nintendo's printed labels by default, so A/B/X/Y read
                // correctly as-is — Switch buttons just aren't colored.
                token.replace(lbRe, QStringLiteral("L"));
                token.replace(rbRe, QStringLiteral("R"));
                token.replace(startRe, QStringLiteral("+"));
                break;
        }
        out += QStringLiteral("<b>[") + token + QStringLiteral("]</b>");
        last = m.capturedEnd();
    }
    out += hint.mid(last);
    out.replace(QStringLiteral("  "), QStringLiteral("&nbsp;&nbsp;"));
    return out;
}
