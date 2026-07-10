// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2024 Prism Launcher Contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "GamepadController.h"
#include <QDebug>
#include <QGuiApplication>

GamepadController::GamepadController(QObject* parent) : QObject(parent)
{
    if (SDL_Init(SDL_INIT_GAMECONTROLLER) < 0) {
        qWarning() << "GamepadController: SDL_Init failed:" << SDL_GetError();
        return;
    }
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            openController(i);
            break;
        }
    }
    connect(&m_pollTimer, &QTimer::timeout, this, &GamepadController::poll);
    m_pollTimer.start(POLL_INTERVAL_MS);

    // While the launcher is in the background (typically: the game it launched is
    // running for hours) 60 Hz polling is wasted work — drop to a slow tick that
    // only keeps hotplug bookkeeping alive.
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        m_pollTimer.start(state == Qt::ApplicationActive ? POLL_INTERVAL_MS : IDLE_POLL_INTERVAL_MS);
    });
}

GamepadController::~GamepadController()
{
    m_pollTimer.stop();
    closeController();
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
}

static GamepadController::PadKind padKindFor(SDL_GameController* controller)
{
    switch (SDL_GameControllerGetType(controller)) {
        case SDL_CONTROLLER_TYPE_PS3:
        case SDL_CONTROLLER_TYPE_PS4:
        case SDL_CONTROLLER_TYPE_PS5:
            return GamepadController::PadKind::PlayStation;
        case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO:
            return GamepadController::PadKind::Nintendo;
        case SDL_CONTROLLER_TYPE_XBOX360:
        case SDL_CONTROLLER_TYPE_XBOXONE:
            return GamepadController::PadKind::Xbox;
        default:
            return GamepadController::PadKind::Generic;
    }
}

void GamepadController::openController(int deviceIndex)
{
    m_controller = SDL_GameControllerOpen(deviceIndex);
    if (m_controller) {
        m_padKind = padKindFor(m_controller);
        qDebug() << "GamepadController: opened" << SDL_GameControllerName(m_controller);
        emit connectionChanged();
    } else {
        qWarning() << "GamepadController: open failed:" << SDL_GetError();
    }
}

void GamepadController::closeController()
{
    if (m_controller) {
        SDL_GameControllerClose(m_controller);
        m_controller = nullptr;
        m_padKind = PadKind::Generic;
        emit connectionChanged();
    }
}

void GamepadController::rumble(quint16 lowFreq, quint16 highFreq, quint32 durationMs)
{
    if (m_controller)
        SDL_GameControllerRumble(m_controller, lowFreq, highFreq, durationMs);
}

void GamepadController::emitDirection(Direction d)
{
    emit activity();
    switch (d) {
        case Left:  emit navigateLeft();  break;
        case Right: emit navigateRight(); break;
        case Up:    emit navigateUp();    break;
        case Down:  emit navigateDown(); break;
        default: break;
    }
}

void GamepadController::dpadUpdate(Direction d, bool pressed)
{
    DpadRepeat& s = m_dpad[d];
    if (pressed) {
        if (!s.held) {
            s.held = true;
            s.ticks = 0;
            emitDirection(d);
        } else {
            s.ticks++;
            if (s.ticks == INITIAL_DELAY_TICKS ||
                (s.ticks > INITIAL_DELAY_TICKS && (s.ticks - INITIAL_DELAY_TICKS) % REPEAT_TICKS == 0))
                emitDirection(d);
        }
    } else {
        s.held  = false;
        s.ticks = 0;
    }
}

void GamepadController::triggerUpdate(int which, bool pressed)
{
    DpadRepeat& s = m_trigger[which];
    auto fire = [this, which] {
        emit activity();
        if (which == 0)
            emit triggerLeftPressed();
        else
            emit triggerRightPressed();
    };
    if (pressed) {
        if (!s.held) {
            s.held = true;
            s.ticks = 0;
            fire();
        } else {
            s.ticks++;
            if (s.ticks == INITIAL_DELAY_TICKS ||
                (s.ticks > INITIAL_DELAY_TICKS && (s.ticks - INITIAL_DELAY_TICKS) % REPEAT_TICKS == 0))
                fire();
        }
    } else {
        s.held  = false;
        s.ticks = 0;
    }
}

void GamepadController::stickUpdate(StickRepeat& s, Direction d, float value, int initialTicks, int repeatTicks)
{
    if (!s.active) {
        // Waiting for stick to cross the entry threshold
        if (value > STICK_ENTER) {
            s.active = true;
            s.ticks  = 0;
            emitDirection(d);  // fire exactly once on entry
        }
    } else {
        // Already active — release when axis drops back below exit threshold
        if (value < STICK_EXIT) {
            s.active = false;
            s.ticks  = 0;
        } else {
            // Auto-repeat for deliberate hold
            s.ticks++;
            if (s.ticks == initialTicks ||
                (s.ticks > initialTicks && (s.ticks - initialTicks) % repeatTicks == 0))
                emitDirection(d);
        }
    }
}

void GamepadController::resetHeldState()
{
    for (int d = 0; d < DirCount; ++d) {
        m_dpad[d] = {};
        m_stick[d] = {};
        m_rstick[d] = {};
    }
    m_trigger[0] = {};
    m_trigger[1] = {};
}

void GamepadController::poll()
{
    // Input only drives the launcher while it's the active app; in the background
    // just keep hotplug bookkeeping alive (the timer is also slowed then).
    const bool active = QGuiApplication::applicationState() == Qt::ApplicationActive;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_CONTROLLERDEVICEADDED:
                if (!m_controller)
                    openController(event.cdevice.which);
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                if (m_controller &&
                    SDL_GameControllerFromInstanceID(event.cdevice.which) == m_controller)
                    closeController();
                break;
            case SDL_CONTROLLERBUTTONDOWN:
                if (!active)
                    break;
                emit activity();
                switch (event.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_A:            emit confirmPressed();       break;
                    case SDL_CONTROLLER_BUTTON_B:            emit cancelPressed();        break;
                    case SDL_CONTROLLER_BUTTON_X:            emit optionsPressed();       break;
                    case SDL_CONTROLLER_BUTTON_Y:            emit infoPressed();          break;
                    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: emit shoulderLeftPressed();  break;
                    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:emit shoulderRightPressed(); break;
                    case SDL_CONTROLLER_BUTTON_START:        emit startPressed();         break;
                    case SDL_CONTROLLER_BUTTON_GUIDE:        emit guidePressed();         break;
                    default: break;
                }
                break;
            default: break;
        }
    }

    if (!m_controller)
        return;
    if (!active) {
        resetHeldState();  // don't fire stale auto-repeats when focus returns
        return;
    }

    // D-pad — fires once per press, auto-repeats while held
    dpadUpdate(Left,  SDL_GameControllerGetButton(m_controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT)  != 0);
    dpadUpdate(Right, SDL_GameControllerGetButton(m_controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) != 0);
    dpadUpdate(Up,    SDL_GameControllerGetButton(m_controller, SDL_CONTROLLER_BUTTON_DPAD_UP)    != 0);
    dpadUpdate(Down,  SDL_GameControllerGetButton(m_controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN)  != 0);

    const float scale = 1.0f / 32767.0f;

    // Left analog stick — hysteresis: fires once per flick, repeats only on deliberate hold
    float ax = SDL_GameControllerGetAxis(m_controller, SDL_CONTROLLER_AXIS_LEFTX) * scale;
    float ay = SDL_GameControllerGetAxis(m_controller, SDL_CONTROLLER_AXIS_LEFTY) * scale;

    // Only use stick if d-pad isn't already driving that direction
    if (!m_dpad[Left].held)  stickUpdate(m_stick[Left],  Left,  -ax, INITIAL_DELAY_TICKS, REPEAT_TICKS);
    if (!m_dpad[Right].held) stickUpdate(m_stick[Right], Right,  ax, INITIAL_DELAY_TICKS, REPEAT_TICKS);
    if (!m_dpad[Up].held)    stickUpdate(m_stick[Up],    Up,    -ay, INITIAL_DELAY_TICKS, REPEAT_TICKS);
    if (!m_dpad[Down].held)  stickUpdate(m_stick[Down],  Down,   ay, INITIAL_DELAY_TICKS, REPEAT_TICKS);

    // Right analog stick — same directions but a much faster repeat: hold it to
    // race through long lists (mod search results, version lists)
    float rx = SDL_GameControllerGetAxis(m_controller, SDL_CONTROLLER_AXIS_RIGHTX) * scale;
    float ry = SDL_GameControllerGetAxis(m_controller, SDL_CONTROLLER_AXIS_RIGHTY) * scale;
    stickUpdate(m_rstick[Left],  Left,  -rx, RSTICK_DELAY_TICKS, RSTICK_REPEAT_TICKS);
    stickUpdate(m_rstick[Right], Right,  rx, RSTICK_DELAY_TICKS, RSTICK_REPEAT_TICKS);
    stickUpdate(m_rstick[Up],    Up,    -ry, RSTICK_DELAY_TICKS, RSTICK_REPEAT_TICKS);
    stickUpdate(m_rstick[Down],  Down,   ry, RSTICK_DELAY_TICKS, RSTICK_REPEAT_TICKS);

    // Triggers — page up/down with button-style auto-repeat
    triggerUpdate(0, SDL_GameControllerGetAxis(m_controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT)  * scale > TRIGGER_THRESHOLD);
    triggerUpdate(1, SDL_GameControllerGetAxis(m_controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) * scale > TRIGGER_THRESHOLD);
}
