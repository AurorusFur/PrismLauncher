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
}

GamepadController::~GamepadController()
{
    m_pollTimer.stop();
    closeController();
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
}

void GamepadController::openController(int deviceIndex)
{
    m_controller = SDL_GameControllerOpen(deviceIndex);
    if (m_controller)
        qDebug() << "GamepadController: opened" << SDL_GameControllerName(m_controller);
    else
        qWarning() << "GamepadController: open failed:" << SDL_GetError();
}

void GamepadController::closeController()
{
    if (m_controller) {
        SDL_GameControllerClose(m_controller);
        m_controller = nullptr;
    }
}

void GamepadController::emitDirection(Direction d)
{
    switch (d) {
        case Left:  emit navigateLeft();  break;
        case Right: emit navigateRight(); break;
        case Up:    emit navigateUp();    break;
        case Down:  emit navigateDown();  break;
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

void GamepadController::stickUpdate(Direction d, float value, float enter, float exit)
{
    StickRepeat& s = m_stick[d];

    if (!s.active) {
        // Waiting for stick to cross the entry threshold
        if (value > enter) {
            s.active = true;
            s.ticks  = 0;
            emitDirection(d);  // fire exactly once on entry
        }
    } else {
        // Already active — release when axis drops back below exit threshold
        if (value < exit) {
            s.active = false;
            s.ticks  = 0;
        } else {
            // Auto-repeat for deliberate hold
            s.ticks++;
            if (s.ticks == INITIAL_DELAY_TICKS ||
                (s.ticks > INITIAL_DELAY_TICKS && (s.ticks - INITIAL_DELAY_TICKS) % REPEAT_TICKS == 0))
                emitDirection(d);
        }
    }
}

void GamepadController::poll()
{
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
                switch (event.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_A: emit confirmPressed(); break;
                    case SDL_CONTROLLER_BUTTON_B: emit cancelPressed();  break;
                    case SDL_CONTROLLER_BUTTON_X: emit optionsPressed(); break;
                    case SDL_CONTROLLER_BUTTON_Y: emit infoPressed();    break;
                    default: break;
                }
                break;
            default: break;
        }
    }

    if (!m_controller)
        return;

    // D-pad — fires once per press, auto-repeats while held
    dpadUpdate(Left,  SDL_GameControllerGetButton(m_controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT)  != 0);
    dpadUpdate(Right, SDL_GameControllerGetButton(m_controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) != 0);
    dpadUpdate(Up,    SDL_GameControllerGetButton(m_controller, SDL_CONTROLLER_BUTTON_DPAD_UP)    != 0);
    dpadUpdate(Down,  SDL_GameControllerGetButton(m_controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN)  != 0);

    // Left analog stick — hysteresis: fires once per flick, repeats only on deliberate hold
    const float scale = 1.0f / 32767.0f;
    float ax = SDL_GameControllerGetAxis(m_controller, SDL_CONTROLLER_AXIS_LEFTX) * scale;
    float ay = SDL_GameControllerGetAxis(m_controller, SDL_CONTROLLER_AXIS_LEFTY) * scale;

    // Only use stick if d-pad isn't already driving that direction
    if (!m_dpad[Left].held)  stickUpdate(Left,  -ax, STICK_ENTER, STICK_EXIT);
    if (!m_dpad[Right].held) stickUpdate(Right,  ax, STICK_ENTER, STICK_EXIT);
    if (!m_dpad[Up].held)    stickUpdate(Up,    -ay, STICK_ENTER, STICK_EXIT);
    if (!m_dpad[Down].held)  stickUpdate(Down,   ay, STICK_ENTER, STICK_EXIT);
}
