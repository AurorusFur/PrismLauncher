// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2024 Prism Launcher Contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "BPOptionsMenu.h"

#include "ui/BPAnim.h"
#include "ui/BPHud.h"
#include "ui/BPStyle.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QShowEvent>
#include <QVariantAnimation>

// Palette-derived button styles so the menu matches the rest of the Big Picture
// UI (settings overlay, dialog host) on any theme.
static QString buttonStyleSelected()
{
    const QPalette& pal = QApplication::palette();
    return QString(
               "QPushButton {"
               "  background-color: %1;"
               "  color: %2;"
               "  font-size: 18px;"
               "  font-weight: bold;"
               "  border: none;"
               "  border-left: 4px solid %3;"
               "  border-radius: 8px;"
               "  text-align: left;"
               "  padding-left: 20px;"
               "}")
        .arg(pal.color(QPalette::Highlight).name(), pal.color(QPalette::HighlightedText).name(),
             pal.color(QPalette::Highlight).lighter(160).name());
}

static QString buttonStyleNormal()
{
    const QPalette& pal = QApplication::palette();
    const QColor dimText = bpDimText(pal);
    return QString(
               "QPushButton {"
               "  background-color: %1;"
               "  color: %2;"
               "  font-size: 18px;"
               "  font-weight: normal;"
               "  border: 1px solid %3;"
               "  border-radius: 8px;"
               "  text-align: left;"
               "  padding-left: 20px;"
               "}")
        .arg(pal.color(QPalette::Base).name(), dimText.name(), pal.color(QPalette::Mid).name());
}

BPOptionsMenu::BPOptionsMenu(QWidget* parent) : QWidget(parent)
{
    // Cover the parent completely and stay on top; no separate window
    setAttribute(Qt::WA_NoSystemBackground, false);
    setFocusPolicy(Qt::StrongFocus);

    const QPalette& pal = QApplication::palette();
    const QColor base = pal.color(QPalette::Base);
    const QColor mid = pal.color(QPalette::Mid);
    const QColor text = pal.color(QPalette::WindowText);

    // Card container
    m_card = new QWidget(this);
    m_card->setStyleSheet(
        QString("QWidget { background: %1; border-radius: 14px; border: 1px solid %2; }").arg(base.name(), mid.name()));

    m_title = new QLabel(this);  // parent = this so it doesn't clip inside card
    {
        QFont f = m_title->font();
        f.setPixelSize(22);
        f.setBold(true);
        m_title->setFont(f);
        m_title->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
        m_title->setStyleSheet(QString("QLabel { color: %1; background: transparent; border: none; }").arg(text.name()));
    }

    // Divider
    auto* divider = new QWidget(m_card);
    divider->setFixedHeight(1);
    divider->setStyleSheet(QString("background: %1;").arg(mid.name()));
    m_card->setProperty("divider", QVariant::fromValue(static_cast<QObject*>(divider)));

    struct Entry { QString label; Action action; };
    const QList<Entry> entries = {
        { tr("Launch"),        Launch     },
        { tr("Edit Settings"), Settings   },
        { tr("Rename"),        Rename     },
        { tr("Copy"),          Copy       },
        { tr("Delete"),        Delete     },
        { tr("Change Icon"),   ChangeIcon },
    };

    for (const auto& e : entries) {
        auto* btn = new QPushButton(e.label, m_card);
        btn->setFixedHeight(BTN_HEIGHT);
        btn->setFocusPolicy(Qt::NoFocus);
        connect(btn, &QPushButton::clicked, this, [this, a = e.action]() {
            hide();
            emit actionSelected(a);
        });
        m_buttons << btn;
    }

    auto* hint = new QLabel(bpHudHtml(tr("[↑↓] Navigate    [A] Select    [B] Cancel")), m_card);
    hint->setTextFormat(Qt::RichText);
    hint->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    hint->setStyleSheet(
        QString("QLabel { color: %1; font-size: 13px; background: transparent; border: none; }").arg(text.name()));
    m_card->setProperty("hint", QVariant::fromValue(static_cast<QObject*>(hint)));

    hide();
}

void BPOptionsMenu::setInstanceName(const QString& name)
{
    m_instanceName = name;
    if (m_title)
        m_title->setText(name);
}

void BPOptionsMenu::showEvent(QShowEvent* event)
{
    // Reset to first item every time the menu is opened
    m_current = 0;
    setHighlight(0);
    // Fill the parent widget
    if (parentWidget())
        setGeometry(0, 0, parentWidget()->width(), parentWidget()->height());
    QWidget::showEvent(event);
    setFocus();

    // Entrance: fade the scrim in while the card pops up. Title is a sibling of
    // the card (see ctor), so it gets the same pop-in to move in lockstep.
    if (auto* old = findChild<QVariantAnimation*>(QStringLiteral("bpScrimAnim"))) {
        old->stop();
        old->deleteLater();
    }
    m_scrimAlpha = 0;
    auto* scrim = new QVariantAnimation(this);
    scrim->setObjectName(QStringLiteral("bpScrimAnim"));
    scrim->setStartValue(0);
    scrim->setEndValue(170);
    scrim->setDuration(150);
    connect(scrim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_scrimAlpha = v.toInt();
        update();
    });
    connect(scrim, &QVariantAnimation::finished, scrim, &QObject::deleteLater);
    scrim->start();

    if (m_card)
        bpPopIn(m_card);
    if (m_title)
        bpPopIn(m_title);
}

void BPOptionsMenu::layoutCard()
{
    if (!m_card || !m_title) return;

    const int numBtns = m_buttons.size();
    const int cardH = CARD_PADDING
                    + TITLE_HEIGHT
                    + 1
                    + CARD_PADDING / 2
                    + numBtns * BTN_HEIGHT
                    + (numBtns - 1) * BTN_SPACING
                    + CARD_PADDING
                    + 28
                    + CARD_PADDING;

    const int cx = (width()  - CARD_WIDTH) / 2;
    const int cy = (height() - cardH)      / 2;
    m_card->setGeometry(cx, cy, CARD_WIDTH, cardH);
    m_title->setGeometry(cx, cy + CARD_PADDING, CARD_WIDTH, TITLE_HEIGHT);

    int y = CARD_PADDING + TITLE_HEIGHT + 4;
    if (auto* div = qobject_cast<QWidget*>(m_card->property("divider").value<QObject*>()))
        div->setGeometry(CARD_PADDING, y, CARD_WIDTH - 2 * CARD_PADDING, 1);
    y += 1 + CARD_PADDING / 2;

    for (auto* btn : m_buttons) {
        btn->setGeometry(CARD_PADDING, y, CARD_WIDTH - 2 * CARD_PADDING, BTN_HEIGHT);
        y += BTN_HEIGHT + BTN_SPACING;
    }
    y += CARD_PADDING - BTN_SPACING;

    if (auto* hint = qobject_cast<QLabel*>(m_card->property("hint").value<QObject*>()))
        hint->setGeometry(0, y, CARD_WIDTH, 28);
}

void BPOptionsMenu::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    layoutCard();
}

void BPOptionsMenu::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    // Translucent scrim — keep the instance grid visible, dimmed, behind the card.
    // Same scrim as the settings overlay's modal cards and the dialog host.
    p.fillRect(rect(), QColor(0, 0, 0, m_scrimAlpha));
}

void BPOptionsMenu::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
        case Qt::Key_Up:
        case Qt::Key_Left:
            navigatePrev();
            break;
        case Qt::Key_Down:
        case Qt::Key_Right:
            navigateNext();
            break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            confirmCurrent();
            break;
        case Qt::Key_Escape:
            dismiss();
            break;
        default:
            QWidget::keyPressEvent(event);
    }
}

void BPOptionsMenu::navigatePrev()
{
    setHighlight((m_current - 1 + m_buttons.size()) % m_buttons.size());
}

void BPOptionsMenu::navigateNext()
{
    setHighlight((m_current + 1) % m_buttons.size());
}

void BPOptionsMenu::confirmCurrent()
{
    if (m_current >= 0 && m_current < m_buttons.size())
        m_buttons[m_current]->click();
}

void BPOptionsMenu::dismiss()
{
    hide();
    emit dismissed();
}

void BPOptionsMenu::setHighlight(int index)
{
    m_current = index;
    const QString selected = buttonStyleSelected();
    const QString normal = buttonStyleNormal();
    for (int i = 0; i < m_buttons.size(); ++i)
        m_buttons[i]->setStyleSheet(i == m_current ? selected : normal);
}
