/* Copyright 2025 Jaakko Keränen <jaakko.keranen@iki.fi>

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

#include "gamepad.h"
#include <SDL2/SDL.h>

#if defined (LAGRANGE_USE_GAMEPAD)

struct Impl_Gamepad {
    int joyIndex;
    SDL_GameController *ctl;
};

iDefineTypeConstruction(Gamepad);

static iBool wasInited_;

static void open_Gamepad_(iGamepad *d, int index) {
    iAssert(d->joyIndex < 0);
    d->joyIndex = index;
    d->ctl      = SDL_GameControllerOpen(index);
    fprintf(stderr, "[Gamepad] using controller: %s\n", SDL_GameControllerNameForIndex(index));
}

static void close_Gamepad_(iGamepad *d) {
    if (d->ctl) {
        SDL_GameControllerClose(d->ctl);
        d->ctl      = NULL;
        d->joyIndex = -1;
        fprintf(stderr, "[Gamepad] controller disconnected\n");
    }
}

/*-----------------------------------------------------------------------------------------------*/

void init_Gamepad(iGamepad *d) {
    d->ctl      = NULL;
    d->joyIndex = -1;
    if (!wasInited_) {
        if (SDL_Init(SDL_INIT_GAMECONTROLLER)) {
            fprintf(stderr, "[Gamepad] failed to initialize: %s\n", SDL_GetError());
            return;
        }
        wasInited_ = iTrue;
    }
    SDL_GameControllerEventState(SDL_ENABLE);
    /* Look for gamepads. */
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            open_Gamepad_(d, i);
            break;
        }
    }
}

void deinit_Gamepad(iGamepad *d) {
    close_Gamepad_(d);
    SDL_GameControllerEventState(SDL_IGNORE);
}

iBool processEvent_Gamepad(iGamepad *d, const SDL_Event *event) {
    switch (event->type) {
        case SDL_CONTROLLERDEVICEADDED: {
            const SDL_ControllerDeviceEvent *dev = &event->cdevice;
            if (!d->ctl) {
                open_Gamepad_(d, dev->which);
            }
            return iTrue;
        }
        case SDL_CONTROLLERDEVICEREMOVED: {
            const SDL_ControllerDeviceEvent *dev = &event->cdevice;
            if (dev->which == d->joyIndex) {
                close_Gamepad_(d);
            }
            return iTrue;
        }
        case SDL_CONTROLLERAXISMOTION: {
            const SDL_ControllerAxisEvent *axis = &event->caxis;

            return iTrue;
        }
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
        {
            return iTrue;
        }
        case SDL_CONTROLLERTOUCHPADDOWN:
        case SDL_CONTROLLERTOUCHPADUP:
        case SDL_CONTROLLERTOUCHPADMOTION: {
            /* Touchpad can be used to move cursor and perform clicks. */

            return iTrue;
        }
    }
    return iFalse;
}

#endif /* LAGRANGE_USE_GAMEPAD */
