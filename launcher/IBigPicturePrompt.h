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

#include <QString>
#include <QStringList>

// Thin abstract interface for controller-friendly confirmation dialogs.
// Registered by BPSettingsOverlay when in Big Picture mode.
// Model-layer code can call instance()->execPrompt() without knowing about widgets.
class IBigPicturePrompt {
public:
    virtual ~IBigPicturePrompt() = default;

    // Show a prompt with the given buttons. Returns the 0-based index of the chosen
    // button, or -1 if cancelled (B button / escape / overlay closed).
    // defaultIndex selects which button starts highlighted.
    virtual int execPrompt(const QString& title, const QString& msg, const QStringList& buttons, int defaultIndex = 0) = 0;

    static IBigPicturePrompt* instance() { return s_instance; }
    static void setInstance(IBigPicturePrompt* p) { s_instance = p; }

private:
    static inline IBigPicturePrompt* s_instance = nullptr;
};
