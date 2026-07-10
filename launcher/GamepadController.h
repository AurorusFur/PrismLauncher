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
    // Physical pad family — the UI maps this to HUD glyph styles.
    enum class PadKind { Generic, Xbox, PlayStation, Nintendo };

    explicit GamepadController(QObject* parent = nullptr);
    ~GamepadController();

    bool isConnected() const { return m_controller != nullptr; }
    PadKind padKind() const { return m_padKind; }

    // Haptic feedback pulse; silently ignored on pads without rumble motors.
    void rumble(quint16 lowFreq, quint16 highFreq, quint32 durationMs);

signals:
    void navigateLeft();
    void navigateRight();
    void navigateUp();
    void navigateDown();
    void confirmPressed();        // A — launch / confirm
    void cancelPressed();         // B — back / close
    void optionsPressed();        // X — options menu
    void infoPressed();           // Y — instance settings
    void shoulderLeftPressed();   // LB — previous group
    void shoulderRightPressed();  // RB — next group
    void startPressed();          // Start — system menu
    void triggerLeftPressed();    // LT — page up (auto-repeats while held)
    void triggerRightPressed();   // RT — page down (auto-repeats while held)
    void guidePressed();          // Guide/Home — back to the instance grid
    void activity();              // any gamepad input — used to hide the mouse cursor
    void connectionChanged();     // pad connected/disconnected (padKind may have changed)

private slots:
    void poll();

private:
    void openController(int deviceIndex);
    void closeController();

    enum Direction { Left, Right, Up, Down, DirCount };
    void emitDirection(Direction d);

    // D-pad: fires once on press, auto-repeats while held
    void dpadUpdate(Direction d, bool pressed);

    // Triggers behave like buttons with d-pad style auto-repeat
    void triggerUpdate(int which, bool pressed);

    // Clears held/repeat state — called when the app loses focus so returning
    // doesn't fire stale auto-repeats.
    void resetHeldState();

    struct StickRepeat {
        bool active = false;  // above exit threshold after having crossed enter
        int  ticks  = 0;
    };
    // Stick: hysteresis prevents multiple fires per flick; auto-repeats after the
    // given hold delay. The left stick mirrors the d-pad; the right stick uses a
    // much faster repeat for fast scrolling through long lists.
    void stickUpdate(StickRepeat& s, Direction d, float value, int initialTicks, int repeatTicks);

    SDL_GameController* m_controller = nullptr;
    PadKind m_padKind = PadKind::Generic;
    QTimer m_pollTimer;

    struct DpadRepeat {
        bool held  = false;
        int  ticks = 0;
    };
    DpadRepeat m_dpad[DirCount];
    DpadRepeat m_trigger[2];  // 0 = LT, 1 = RT

    StickRepeat m_stick[DirCount];   // left stick — navigation
    StickRepeat m_rstick[DirCount];  // right stick — fast scroll

    static constexpr int   INITIAL_DELAY_TICKS = 22;   // ~350 ms
    static constexpr int   REPEAT_TICKS        = 8;    // ~128 ms
    static constexpr int   RSTICK_DELAY_TICKS  = 8;    // ~128 ms — fast scroll kicks in quickly
    static constexpr int   RSTICK_REPEAT_TICKS = 4;    // ~64 ms
    static constexpr int   POLL_INTERVAL_MS      = 16;
    static constexpr int   IDLE_POLL_INTERVAL_MS = 250;  // app inactive (e.g. game running)
    static constexpr float STICK_ENTER         = 0.55f;
    static constexpr float STICK_EXIT          = 0.20f;
    static constexpr float TRIGGER_THRESHOLD   = 0.5f;
};
