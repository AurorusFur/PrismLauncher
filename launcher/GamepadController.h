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

#include <QObject>
#include <QTimer>
#include <SDL.h>

class GamepadController : public QObject {
    Q_OBJECT
public:
    explicit GamepadController(QObject* parent = nullptr);
    ~GamepadController();

    bool isConnected() const { return m_controller != nullptr; }

signals:
    void navigateLeft();
    void navigateRight();
    void navigateUp();
    void navigateDown();
    void confirmPressed();   // A — launch
    void cancelPressed();    // B — back
    void optionsPressed();   // X — context menu
    void infoPressed();      // Y — instance settings

private slots:
    void poll();

private:
    void openController(int deviceIndex);
    void closeController();

    enum Direction { Left, Right, Up, Down, DirCount };
    void emitDirection(Direction d);

    // D-pad: fires once on press, auto-repeats while held
    void dpadUpdate(Direction d, bool pressed);

    // Stick: hysteresis prevents multiple fires per flick;
    //        auto-repeats only after a deliberate hold
    void stickUpdate(Direction d, float value, float enter, float exit);

    SDL_GameController* m_controller = nullptr;
    QTimer m_pollTimer;

    struct DpadRepeat {
        bool held  = false;
        int  ticks = 0;
    };
    DpadRepeat m_dpad[DirCount];

    // Stick hysteresis state (independent of dpad)
    struct StickRepeat {
        bool active = false;  // above exit threshold after having crossed enter
        int  ticks  = 0;
    };
    StickRepeat m_stick[DirCount];

    static constexpr int   INITIAL_DELAY_TICKS = 22;   // ~350 ms
    static constexpr int   REPEAT_TICKS        = 8;    // ~128 ms
    static constexpr int   POLL_INTERVAL_MS    = 16;
    static constexpr float STICK_ENTER         = 0.55f;
    static constexpr float STICK_EXIT          = 0.20f;
};
