/* Copyright 2013-2021 MultiMC Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <QCache>
#include <QHash>
#include <QPainterPath>
#include <QStaticText>
#include <QStyledItemDelegate>

class ListViewDelegate : public QStyledItemDelegate {
    Q_OBJECT

   public:
    explicit ListViewDelegate(QObject* parent = 0);
    virtual ~ListViewDelegate() {}

    void setBigPictureMode(bool enabled);

    static constexpr int BP_ITEM_WIDTH = 260;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override;

   signals:
    void textChanged(QString before, QString after) const;

   private slots:
    void editingDone();

   private:
    void paintBigPicture(QPainter* painter, const QStyleOptionViewItem& opt, const QModelIndex& index) const;

    static constexpr int BP_ICON_SIZE = 190;
    static constexpr int BP_ITEM_HEIGHT = 300;
    static constexpr int BP_CARD_MARGIN = 6;
    static constexpr int BP_CARD_RADIUS = 12;
    static constexpr int BP_BORDER_WIDTH = 4;

    int iconPixelSize() const { return m_bigPicture ? BP_ICON_SIZE : 48; }
    int itemPixelWidth() const { return m_bigPicture ? BP_ITEM_WIDTH : 100; }

    bool m_bigPicture = false;

    // Paint caches for the Big Picture cards. Every selection move repaints all
    // visible cards; laying out wrapped text and building clip paths per card per
    // frame is the expensive part, and both depend only on values that rarely
    // change (name text, card size).
    mutable QHash<QString, QStaticText> m_bpNameCache;
    mutable QPainterPath m_bpCardClip;
    mutable QSize m_bpCardClipSize;
};
