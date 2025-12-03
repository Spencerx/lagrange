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

#include "keyboardwidget.h"
#include "root.h"

#include <SDL_timer.h>

static int initialRepeatDelayMs_ = 250;
static int repeatDelayMs_        = 75;

iDeclareType(KeyPage);
iDeclareType(Key);

// enum iKeyType {
    // character_KeyType,
    // pageSwitch_KeyType,
// };

enum iKeyFlags {
    pressed_KeyFlag = iBit(1),
};

struct Impl_Key {
    int      flags;
    int      keySym;
    iString *character;
    int      pageId;
};

struct Impl_KeyPage {
    int id;
};

struct Impl_KeyboardWidget {
    iWidget     widget;
    int         pageId; /* visible page ID */
    SDL_TimerID repeatTimer;
    int         height; /* depends on screen dimensions, key layout */
};

iDefineObjectConstruction(KeyboardWidget);

static void updateHeight_KeyboardWidget_(iKeyboardWidget *d) {
    iWidget *w = as_Widget(d);
    const int maxHeight = size_Root(as_Widget(d)->root).y / 2;
    const int maxRows = 5;
    int rowHeight = maxHeight / maxRows;
    int numRows = 5; /* TODO: check from current page! */
    setFixedSize_Widget(w, init_I2(-1, numRows * rowHeight));
    arrange_Widget(w);
    refresh_Widget(w);
}

static void showOrHide_KeyboardWidget_(iKeyboardWidget *d, iBool show) {

}

/*-----------------------------------------------------------------------------------------------*/

void init_KeyboardWidget(iKeyboardWidget *d) {
    iWidget *w = &d->widget;
    init_Widget(w);
    d->repeatTimer = 0;
    setFlags_Widget(w,
                    fixedHeight_WidgetFlag | resizeToParentWidth_WidgetFlag |
                        moveToParentBottomEdge_WidgetFlag,
                    iTrue);
}

void deinit_KeyboardWidget(iKeyboardWidget *d) {
    if (d->repeatTimer) {
        SDL_RemoveTimer(d->repeatTimer);
    }
}

static void draw_KeyboardWidget_(const iKeyboardWidget *d) {

}

static iBool processEvent_KeyboardWidget_(iKeyboardWidget *d, const SDL_Event *event) {
    return processEvent_Widget(&d->widget, event);
}

iBeginDefineSubclass(KeyboardWidget, Widget)
    .draw         = (iAny *) draw_KeyboardWidget_,
    .processEvent = (iAny *) processEvent_KeyboardWidget_,
iEndDefineSubclass(KeyboardWidget)
