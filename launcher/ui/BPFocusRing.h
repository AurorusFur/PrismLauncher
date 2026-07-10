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
#include <QApplication>
#include <QEasingCurve>
#include <QPainter>
#include <QPen>
#include <QPropertyAnimation>
#include <QVariantAnimation>
#include <QWidget>

// Console-style focus indicator: a highlight-colored rounded ring drawn on top of
// whatever widget currently has keyboard focus. The native dotted focus rectangle
// is invisible from a couch; this is not. The ring glides between controls
// instead of jumping, and breathes gently while idle. Shared by BPSettingsOverlay
// (page content) and BPDialogHost (hosted dialogs).
class FocusRingWidget : public QWidget {
public:
    explicit FocusRingWidget(QWidget* parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);

        m_moveAnim = new QPropertyAnimation(this, "geometry", this);
        m_moveAnim->setDuration(140);
        m_moveAnim->setEasingCurve(QEasingCurve::OutCubic);

        // Subtle idle pulse — a 0→1 sawtooth folded into a 0→1→0 breathe in paint.
        // The animation ticks at ~60 fps but the breathe is slow; repainting is
        // quantized to PULSE_STEPS frames per cycle so the ring isn't the most
        // active painter in the app while it just sits there.
        m_pulseAnim = new QVariantAnimation(this);
        m_pulseAnim->setStartValue(0.0);
        m_pulseAnim->setEndValue(1.0);
        m_pulseAnim->setDuration(1600);
        m_pulseAnim->setLoopCount(-1);
        connect(m_pulseAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            const int step = int(v.toDouble() * PULSE_STEPS);
            if (step != m_lastPulseStep) {
                m_lastPulseStep = step;
                update();
            }
        });

        hide();
    }

    // Glide the ring to a new control. Snaps when the ring isn't visible yet —
    // gliding in from wherever it last was looks broken, not smooth.
    void moveTo(const QRect& r)
    {
        if (r == m_target)
            return;
        m_target = r;
        m_moveAnim->stop();
        if (!isVisible()) {
            setGeometry(r);
            return;
        }
        m_moveAnim->setStartValue(geometry());
        m_moveAnim->setEndValue(r);
        m_moveAnim->start();
    }

protected:
    void showEvent(QShowEvent* ev) override
    {
        m_pulseAnim->start();
        QWidget::showEvent(ev);
    }

    void hideEvent(QHideEvent* ev) override
    {
        m_pulseAnim->stop();
        m_target = QRect();  // next appearance snaps instead of gliding from a stale spot
        QWidget::hideEvent(ev);
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        // While a value is being edited (spinbox/text edit mode) the ring turns
        // amber so the user can tell "navigating" apart from "adjusting".
        QColor hl = property("editing").toBool() ? QColor(0xd8, 0xb9, 0x44)
                                                 : QApplication::palette().color(QPalette::Highlight);
        const double phase = double(m_lastPulseStep) / PULSE_STEPS;
        const double breathe = 1.0 - qAbs(2.0 * phase - 1.0);  // 0→1→0
        hl.setAlpha(255 - int(breathe * 70));
        QColor fill = hl;
        fill.setAlpha(22 + int(breathe * 12));
        p.setPen(QPen(hl, 3));
        p.setBrush(fill);
        p.drawRoundedRect(QRectF(rect()).adjusted(2, 2, -2, -2), 6, 6);
    }

private:
    static constexpr int PULSE_STEPS = 24;  // repaints per breathe cycle (~15/s)

    QPropertyAnimation* m_moveAnim = nullptr;
    QVariantAnimation* m_pulseAnim = nullptr;
    QRect m_target;
    int m_lastPulseStep = 0;
};

// Places the ring over the app's focused widget, hiding it when focus is outside
// `scope`, on an item view (views draw their own row highlight), or too small to
// ring. `host` is the widget the ring lives in; `scope` must be a direct child of
// `host` (its geometry is used as the clamp rect). Callers do their own mode/
// visibility gating before calling.
inline void bpPositionFocusRing(FocusRingWidget* ring, QWidget* host, QWidget* scope)
{
    if (!ring)
        return;
    QWidget* fw = QApplication::focusWidget();
    if (!scope || !fw || !scope->isAncestorOf(fw)) {
        ring->hide();
        return;
    }
    // (Focus may sit on the view itself or on its viewport via focus proxy.)
    if (qobject_cast<QAbstractItemView*>(fw) ||
        (fw->parentWidget() && qobject_cast<QAbstractItemView*>(fw->parentWidget()))) {
        ring->hide();
        return;
    }
    QRect r(fw->mapTo(host, QPoint(0, 0)), fw->size());
    r.adjust(-5, -5, 5, 5);
    r &= scope->geometry();  // don't spill over the surrounding chrome
    if (r.width() < 8 || r.height() < 8) {
        ring->hide();
        return;
    }
    ring->moveTo(r);
    ring->show();
    ring->raise();
}
