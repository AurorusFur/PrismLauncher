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

#include "CustomMessageBox.h"

#include <QAbstractButton>
#include <QApplication>

#include "IBigPicturePrompt.h"

namespace CustomMessageBox {

namespace {
// While Big Picture mode's settings overlay is open, exec() routes to the
// controller-friendly inline prompt card instead of opening a desktop dialog
// that a gamepad can't reach. show()n info boxes are unaffected.
class BPAwareMessageBox : public QMessageBox {
   public:
    using QMessageBox::QMessageBox;

    int exec() override
    {
        auto* prompt = IBigPicturePrompt::instance();
        if (!prompt)
            return QMessageBox::exec();
        // If another dialog is already up — a real modal window, or one hosted
        // in-window by BPDialogHost — the overlay's prompt card would sit *behind*
        // it and be unreachable, freezing the launcher. Fall back to a plain box:
        // in Big Picture mode the dialog host picks it up as an in-window card on
        // top, where the gamepad can drive it.
        if (QApplication::activeModalWidget() || qApp->property("bpDialogHostActive").toBool())
            return QMessageBox::exec();

        // Standard buttons in ascending enum order (Ok, Yes, No, Abort, Cancel…)
        QList<QMessageBox::StandardButton> order;
        QStringList labels;
        const auto stdButtons = standardButtons();
        for (uint bit = QMessageBox::FirstButton; bit <= QMessageBox::LastButton; bit <<= 1) {
            const auto sb = static_cast<QMessageBox::StandardButton>(bit);
            if (!stdButtons.testFlag(sb))
                continue;
            QAbstractButton* btn = button(sb);
            if (!btn)
                continue;
            order << sb;
            labels << btn->text().remove(u'&').trimmed();
        }
        if (order.isEmpty())
            return QMessageBox::exec();

        int defaultIndex = 0;
        if (defaultButton())
            defaultIndex = qMax(0, static_cast<int>(order.indexOf(standardButton(defaultButton()))));

        const int choice = prompt->execPrompt(windowTitle(), text(), labels, defaultIndex);
        if (choice >= 0 && choice < order.size())
            return order[choice];
        // Cancelled with B — behave like Escape: prefer a rejecting button.
        if (stdButtons.testFlag(QMessageBox::Cancel))
            return QMessageBox::Cancel;
        if (stdButtons.testFlag(QMessageBox::No))
            return QMessageBox::No;
        if (stdButtons.testFlag(QMessageBox::Close))
            return QMessageBox::Close;
        return order.first();
    }
};
}  // namespace

QMessageBox* selectable(QWidget* parent,
                        const QString& title,
                        const QString& text,
                        QMessageBox::Icon icon,
                        QMessageBox::StandardButtons buttons,
                        QMessageBox::StandardButton defaultButton,
                        QCheckBox* checkBox)
{
    QMessageBox* messageBox = new BPAwareMessageBox(parent);
    messageBox->setWindowTitle(title);
    messageBox->setText(text);
    messageBox->setStandardButtons(buttons);
    messageBox->setDefaultButton(defaultButton);
    messageBox->setTextInteractionFlags(Qt::TextSelectableByMouse);
    messageBox->setIcon(icon);
    messageBox->setTextInteractionFlags(Qt::TextBrowserInteraction);
    if (checkBox)
        messageBox->setCheckBox(checkBox);

    return messageBox;
}
}  // namespace CustomMessageBox
