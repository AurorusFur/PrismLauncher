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

#include <QAbstractAnimation>
#include <QEasingCurve>
#include <QGraphicsOpacityEffect>
#include <QParallelAnimationGroup>
#include <QPointer>
#include <QPropertyAnimation>
#include <QWidget>

// Entrance animations for Big Picture surfaces. Console UIs feel fast, not
// floaty — durations stay under 200 ms. Closes are intentionally instant.

// Stops a still-running entrance animation (the surface was re-opened before the
// previous one finished) so stale start/end geometry can't be applied. Callers
// must set the widget's final geometry *before* starting a new animation.
inline void bpStopEntranceAnim(QWidget* w)
{
    const auto anims =
        w->findChildren<QAbstractAnimation*>(QStringLiteral("bpEntranceAnim"), Qt::FindDirectChildrenOnly);
    for (auto* anim : anims) {
        anim->stop();
        delete anim;
    }
    if (w->graphicsEffect())
        w->setGraphicsEffect(nullptr);
}

// Fade + short upward slide for cards (action menus, prompts, version pickers).
// Call after the card has been positioned and shown. The opacity effect is
// removed once done — leaving effects installed breaks some styled children.
inline void bpPopIn(QWidget* w, int slidePx = 16, int ms = 150)
{
    bpStopEntranceAnim(w);
    const QRect end = w->geometry();

    auto* eff = new QGraphicsOpacityEffect(w);
    eff->setOpacity(0.0);
    w->setGraphicsEffect(eff);

    auto* group = new QParallelAnimationGroup(w);
    group->setObjectName(QStringLiteral("bpEntranceAnim"));

    auto* fade = new QPropertyAnimation(eff, "opacity", group);
    fade->setDuration(ms);
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->setEasingCurve(QEasingCurve::OutCubic);
    group->addAnimation(fade);

    auto* slide = new QPropertyAnimation(w, "geometry", group);
    slide->setDuration(ms);
    slide->setStartValue(end.translated(0, slidePx));
    slide->setEndValue(end);
    slide->setEasingCurve(QEasingCurve::OutCubic);
    group->addAnimation(slide);

    QObject::connect(group, &QParallelAnimationGroup::finished, w, [w, peff = QPointer<QGraphicsOpacityEffect>(eff)] {
        if (peff && w->graphicsEffect() == peff)
            w->setGraphicsEffect(nullptr);
    });
    QObject::connect(group, &QParallelAnimationGroup::finished, group, &QObject::deleteLater);
    group->start();
}

// Geometry-only upward slide for full-screen surfaces (settings overlay,
// resource browser, on-screen keyboard). No opacity effect: rendering a whole
// PageContainer through QGraphicsOpacityEffect stutters; a sheet slide doesn't.
inline void bpSlideIn(QWidget* w, int slidePx = 48, int ms = 180)
{
    bpStopEntranceAnim(w);
    const QRect end = w->geometry();

    auto* slide = new QPropertyAnimation(w, "geometry", w);
    slide->setObjectName(QStringLiteral("bpEntranceAnim"));
    slide->setDuration(ms);
    slide->setStartValue(end.translated(0, slidePx));
    slide->setEndValue(end);
    slide->setEasingCurve(QEasingCurve::OutCubic);
    QObject::connect(slide, &QPropertyAnimation::finished, slide, &QObject::deleteLater);
    slide->start();
}
