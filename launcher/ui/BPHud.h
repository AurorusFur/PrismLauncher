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

// Formats a plain controller HUD hint like "[A] Select    [B] Cancel" as rich HTML:
// face buttons get their standard controller colors (A green, B red, X blue, Y yellow),
// all bracketed tokens are bold, and space runs survive HTML whitespace collapsing.
// Labels showing the result must use Qt::RichText.
inline QString bpHudHtml(QString hint)
{
    hint = hint.toHtmlEscaped();

    static const QRegularExpression tokenRe(QStringLiteral("\\[([^\\]]+)\\]"));
    // Only standalone letters are colored: the B in "LB/RB" has no word boundary before it.
    static const QRegularExpression aRe(QStringLiteral("\\bA\\b"));
    static const QRegularExpression bRe(QStringLiteral("\\bB\\b"));
    static const QRegularExpression xRe(QStringLiteral("\\bX\\b"));
    static const QRegularExpression yRe(QStringLiteral("\\bY\\b"));

    QString out;
    qsizetype last = 0;
    auto it = tokenRe.globalMatch(hint);
    while (it.hasNext()) {
        const auto m = it.next();
        out += hint.mid(last, m.capturedStart() - last);
        QString token = m.captured(1);
        token.replace(aRe, QStringLiteral("<span style='color:#59b55f'>A</span>"));
        token.replace(bRe, QStringLiteral("<span style='color:#e0605e'>B</span>"));
        token.replace(xRe, QStringLiteral("<span style='color:#5a9bd8'>X</span>"));
        token.replace(yRe, QStringLiteral("<span style='color:#d8b944'>Y</span>"));
        out += QStringLiteral("<b>[") + token + QStringLiteral("]</b>");
        last = m.capturedEnd();
    }
    out += hint.mid(last);
    out.replace(QStringLiteral("  "), QStringLiteral("&nbsp;&nbsp;"));
    return out;
}
