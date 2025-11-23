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

#if defined (LAGRANGE_USE_GAMEPAD)

#include "app.h"
#include "documentwidget.h"
#include "labelwidget.h"
#include "paint.h"
#include "util.h"
#include "window.h"
#include <SDL2/SDL.h>

struct Impl_Gamepad {
    int                 joyIndex;
    SDL_GameController *ctl;
    iWindow            *window; /* we assume there is one window and it won't change */

    float               scrollSpeed;
    float               scrollAccum;     /* pixels */
    iBool               isScrollCancelled;

    float               pointerSpeed[2]; /* pixels */
    float               pointerf[2];     /* pixels */
    iInt2               pointer;         /* points (for SDL event) */
    iInt2               lastPointer;     /* points (for SDL event) */
    iAnim               opacity;

    unsigned int buttons; /* bits */
    int          primary; /* e.g., SDL_CONTROLLER_BUTTON_A */
    int          secondary;
};

iDefineTypeConstruction(Gamepad);

static iBool wasInited_;

static void ticker_Gamepad_(void *);

static iRoot *root_Gamepad_(const iGamepad *d) {
    return d->window->roots[0];
}

static void open_Gamepad_(iGamepad *d, int index) {
    iAssert(d->joyIndex < 0);
    d->joyIndex = index;
    d->ctl      = SDL_GameControllerOpen(index);
    fprintf(stderr, "[Gamepad] using controller: %s\n", SDL_GameControllerNameForIndex(index));
    /* TODO: Can we determine the type of controller? */
    d->primary   = SDL_CONTROLLER_BUTTON_A;
    d->secondary = SDL_CONTROLLER_BUTTON_B;
    d->isScrollCancelled = iFalse;
}

static void close_Gamepad_(iGamepad *d) {
    if (d->ctl) {
        SDL_GameControllerClose(d->ctl);
        d->ctl      = NULL;
        d->joyIndex = -1;
        fprintf(stderr, "[Gamepad] controller disconnected\n");
    }
}

static void addTicker_Gamepad_(iGamepad *d) {
    addTickerRoot_App(ticker_Gamepad_, root_Gamepad_(d), d);
}

void movePointerOntoWidget_Gamepad(iGamepad *d, const iWidget *widget) {
    if (widget) {
        const iInt2 mid = mid_Rect(boundsWithoutVisualOffset_Widget(widget));
        d->pointerf[0] = mid.x;
        d->pointerf[1] = mid.y;
        postRefresh_Window(d->window);
    }
}

static void pointerOntoFocus_Gamepad_(iGamepad *d) {
    movePointerOntoWidget_Gamepad(d, focus_Widget());
}

static void ticker_Gamepad_(void *context) {
    iGamepad *d = context;
    const float elapsed = elapsedSinceLastTicker_App() / 1000.0f;
    if (d->isScrollCancelled) {
        d->scrollSpeed = 0;
    }
    d->scrollAccum += d->scrollSpeed * 250 * gap_UI * elapsed;
    const float maxPointer[2] = { d->window->size.x, d->window->size.y };
    iForIndices(i, d->pointerf) {
        d->pointerf[i] += d->pointerSpeed[i] * 100 * gap_UI * elapsed;
        d->pointerf[i] = iClamp(d->pointerf[i], 0, maxPointer[i]);
    }
    /* Post a wheel scroll event. */ {
        const int pixels = (int)(fabs(d->scrollAccum)) * iSign(d->scrollAccum);
        if (pixels) {
            SDL_PushEvent((SDL_Event *) &(SDL_MouseWheelEvent) {
                .type      = SDL_MOUSEWHEEL,
                .which     = mouseId_Gamepad,
                .windowID  = id_Window(d->window),
                .y         = -pixels,
                .mouseX    = d->pointer.x,
                .mouseY    = d->pointer.y,
                .direction = perPixel_MouseWheelFlag,
            });
            d->scrollAccum -= pixels;
        }
    }
    /* Post pointer movement. */ {
        d->pointer.x = (int) (d->pointerf[0] / d->window->pixelRatio);
        d->pointer.y = (int) (d->pointerf[1] / d->window->pixelRatio);
        const iInt2 delta = sub_I2(d->lastPointer, d->pointer);
        if (delta.x || delta.y) {
            SDL_PushEvent((SDL_Event *) &(SDL_MouseMotionEvent) {
                .type      = SDL_MOUSEMOTION,
                .which     = mouseId_Gamepad,
                .windowID  = id_Window(d->window),
                .x         = d->pointer.x,
                .y         = d->pointer.y,
                .xrel      = delta.x,
                .yrel      = delta.y,
            });
            d->lastPointer = d->pointer;
            postRefresh_Window(d->window);
        }
    }
    if (d->scrollSpeed || d->pointerSpeed[0] || d->pointerSpeed[1]) {
        /* Keep scrolling. */
        addTicker_Gamepad_(d);
    }
}

static void animate_Gamepad_(void *context) {
    iGamepad *d = context;
    postRefresh_Window(d->window);
    if (!isFinished_Anim(&d->opacity)) {
        addTickerRoot_App(animate_Gamepad_, root_Gamepad_(d), context);
    }
}

static void showPointer_Gamepad_(iGamepad *d) {
    if (targetValue_Anim(&d->opacity) < 1) {
        setValue_Anim(&d->opacity, 1, 160);
        animate_Gamepad_(d);
    }
    /* TODO: Fade the pointer away after some time .*/
}

static void hidePointer_Gamepad_(iGamepad *d) {
    if (targetValue_Anim(&d->opacity) > 0) {
        setValue_Anim(&d->opacity, 0, 320);
        animate_Gamepad_(d);
    }
}

/*-----------------------------------------------------------------------------------------------*/

void init_Gamepad(iGamepad *d) {
    d->ctl            = NULL;
    d->joyIndex       = -1;
    d->scrollSpeed    = 0;
    d->scrollAccum    = 0;
    d->window         = (iWindow *) constFront_PtrArray(mainWindows_App());
    init_Anim(&d->opacity, 0);
    /* Place the soft pointer at the center of the window. */
    d->pointer     = mid_Rect(initSize_Rect(d->window->size.x / d->window->pixelRatio,
                                            d->window->size.y / d->window->pixelRatio));
    d->lastPointer = d->pointer;
    d->pointerf[0] = d->pointer.x * d->window->pixelRatio;
    d->pointerf[1] = d->pointer.y * d->window->pixelRatio;
    d->buttons = 0;
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

iBool isConnected_Gamepad(const iGamepad *d) {
    return d && d->ctl != NULL;
}

iInt2 pointerCoord_Gamepad(const iGamepad *d) {
    return d ? d->pointer : zero_I2();
}

static iBool moveFocusToDirection_Gamepad_(iGamepad *d, int button) {
    if (!focus_Widget() || isInstance_Object(focus_Widget(), &Class_DocumentWidget)) {
        return iFalse;
    }
    int key = 0;
    switch (button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            key = SDLK_UP;
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            key = SDLK_DOWN;
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            key = SDLK_LEFT;
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            key = SDLK_RIGHT;
            break;
    }
    if (key) {
        emulateKeyPress_Window(d->window, key, 0);
        pointerOntoFocus_Gamepad_(d);
        return iTrue;
    }
    return iFalse;
}

iBool processEvent_Gamepad(iGamepad *d, const void *sdlEvent) {
    const SDL_Event *event = sdlEvent;
    if (isCommand_UserEvent(sdlEvent, "focus.gained")) {
        pointerOntoFocus_Gamepad_(d);
        return iFalse;
    }
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
            const float deadZone = 0.1f;
            // fprintf(stderr, "[Gamepad] axis:%d value:%5d\n", axis->axis, axis->value);
            float norm = axis->value / (float) SDL_JOYSTICK_AXIS_MAX;
            const int pointerAxis = (axis->axis == SDL_CONTROLLER_AXIS_LEFTY ? 1 : 0);
            if (fabs(norm) < deadZone) {
                if (axis->axis == SDL_CONTROLLER_AXIS_RIGHTY &&
                    !(d->buttons & ((1 << SDL_CONTROLLER_BUTTON_DPAD_UP) |
                                    (1 << SDL_CONTROLLER_BUTTON_DPAD_DOWN)))) {
                    d->scrollSpeed = 0;
                    d->isScrollCancelled = iFalse;
                }
                else if (axis->axis == SDL_CONTROLLER_AXIS_LEFTX ||
                         axis->axis == SDL_CONTROLLER_AXIS_LEFTY) {
                    d->pointerSpeed[pointerAxis] = 0;
                }
                return iTrue;
            }
            norm = iClamp((norm - iSign(norm) * deadZone) / (1.0f - deadZone), -1.0f, 1.0f);
            if (axis->axis == SDL_CONTROLLER_AXIS_RIGHTY) {
                d->scrollSpeed = norm * norm * iSignf(norm);
                hidePointer_Gamepad_(d);
                addTicker_Gamepad_(d);
            }
            else if (axis->axis == SDL_CONTROLLER_AXIS_LEFTX ||
                     axis->axis == SDL_CONTROLLER_AXIS_LEFTY) {
                d->pointerSpeed[pointerAxis] = norm;
                if (focus_Widget()) setFocus_Widget(NULL);
                showPointer_Gamepad_(d);
                addTicker_Gamepad_(d);
            }
            return iTrue;
        }
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP: {
            const SDL_ControllerButtonEvent *but = &event->cbutton;
            // fprintf(stderr, "[Gamepad] button:%x st:%d\n", but->button, but->state);
            const iBool isPress = (but->state != 0);
            iChangeFlags(d->buttons, 1 << but->button, isPress);
            if (but->button == d->primary || but->button == d->secondary) {
                if (value_Anim(&d->opacity) < 0.5f) {
                    /* If the pointer is invisible, it must appear first. */
                    showPointer_Gamepad_(d);
                    if (!focus_Widget()) return iTrue;
                }
                const int button = (but->button == d->primary ? SDL_BUTTON_LEFT : SDL_BUTTON_RIGHT);
                SDL_PushEvent((SDL_Event *) &(SDL_MouseButtonEvent) {
                    .type     = (isPress ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP),
                    .which    = mouseId_Gamepad,
                    .windowID = id_Window(d->window),
                    .button   = button,
                    .state    = (isPress ? SDL_PRESSED : SDL_RELEASED),
                    .clicks   = 1,
                    .x        = d->pointer.x,
                    .y        = d->pointer.y,
                });
            }
            else if (but->button == SDL_CONTROLLER_BUTTON_DPAD_LEFT && isPress) {
                if (moveFocusToDirection_Gamepad_(d, but->button)) return iTrue;
                postCommand_Root(root_Gamepad_(d), "navigate.back");
            }
            else if (but->button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT && isPress) {
                if (moveFocusToDirection_Gamepad_(d, but->button)) return iTrue;
                postCommand_Root(root_Gamepad_(d), "navigate.forward");
            }
            else if (but->button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
                if (isPress) {
                    if (moveFocusToDirection_Gamepad_(d, but->button)) return iTrue;
                }
                d->scrollSpeed = (isPress ? -0.75f : 0);
                addTicker_Gamepad_(d);
            }
            else if (but->button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
                if (isPress) {
                    if (moveFocusToDirection_Gamepad_(d, but->button)) return iTrue;
                }
                d->scrollSpeed = (isPress ? 0.75f : 0);
                addTicker_Gamepad_(d);
            }
            else if (but->button == SDL_CONTROLLER_BUTTON_RIGHTSTICK && isPress) {
                if (d->scrollSpeed < 0) {
                    postCommand_Root(root_Gamepad_(d), "scroll.top smooth:1");
                    d->isScrollCancelled = iTrue;
                }
                else if (d->scrollSpeed > 0) {
                    postCommand_Root(root_Gamepad_(d), "scroll.bottom smooth:1");
                    d->isScrollCancelled = iTrue;
                }
            }
            else if (but->button == SDL_CONTROLLER_BUTTON_START && isPress) {
                iRoot *root = root_Gamepad_(d);
                showToolbar_Root(root, iTrue);
                iWidget *nav = findChild_Widget(root->widget, "toolbar.navmenu");
                emulateMouseClick_Widget(nav, SDL_BUTTON_LEFT);
            }
            else if (but->button == SDL_CONTROLLER_BUTTON_BACK && isPress) {
                postCommand_Root(root_Gamepad_(d), "sidebar.toggle");
            }
            else if (but->button == SDL_CONTROLLER_BUTTON_X && isPress) {
                emulateKeyPress_Window(d->window, SDLK_ESCAPE, 0);
            }
            else if (but->button == SDL_CONTROLLER_BUTTON_Y && isPress) {
                postCommand_Root(root_Gamepad_(d), "navigate.focus");
            }
            else if ((but->button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER ||
                      but->button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) &&
                     isPress) {
                if (isVisible_Widget(findWidget_App("sidebar"))) {
                    postCommandf_Root(root_Gamepad_(d),
                                      "sidebar.cycle arg:%d",
                                      but->button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER ? -1 : +1);
                }
                else {
                    postCommand_Root(root_Gamepad_(d),
                                     but->button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER
                                         ? "tabs.prev"
                                         : "tabs.next");
                }
            }
            return iTrue;
        }
        case SDL_CONTROLLERTOUCHPADDOWN:
        case SDL_CONTROLLERTOUCHPADUP:
        case SDL_CONTROLLERTOUCHPADMOTION: {
            /* Touchpad can be used to move cursor and perform clicks. */
            const SDL_ControllerTouchpadEvent *pad = &event->ctouchpad;
            fprintf(stderr, "[Gamepad] touchpad type:%d x:%f y:%f\n", pad->type, pad->x, pad->y);
            return iTrue;
        }
    }
    return iFalse;
}

void draw_Gamepad(const iGamepad *d) {
    /* Draw the pointer. */
    iPaint p;
    init_Paint(&p);
    SDL_Renderer *render = renderer_Window(get_Window());
    SDL_SetRenderDrawBlendMode(render, SDL_BLENDMODE_BLEND);
    p.alpha = value_Anim(&d->opacity) * 255;
    fillRect_Paint(
        &p, (iRect) { init_I2(d->pointerf[0], d->pointerf[1]), init1_I2(gap_UI * 3) }, red_ColorId);
    SDL_SetRenderDrawBlendMode(render, SDL_BLENDMODE_NONE);
    /* TODO: Draw button help overlay? */
}

#else

/*- Dummy --------------------------------------------------------------------------------------*/

struct Impl_Gamepad {};

iDefineTypeConstruction(Gamepad);

void init_Gamepad   (iGamepad *d) { iUnused(d); }
void deinit_Gamepad (iGamepad *d) { iUnused(d); }
void draw_Gamepad   (const iGamepad *d) { iUnused(d); }

void movePointerOntoWidget_Gamepad(iGamepad *d, const iWidget *widget) {
    iUnused(d, widget);
}

iBool isConnected_Gamepad(const iGamepad *d) {
    iUnused(d);
    return iFalse;
}

iInt2 pointerCoord_Gamepad(const iGamepad *d) {
    iUnused(d);
    return zero_I2();
}

iBool processEvent_Gamepad(iGamepad *d, const void *event) {
    iUnused(d, event);
    return iFalse;
}

#endif /* LAGRANGE_USE_GAMEPAD */
