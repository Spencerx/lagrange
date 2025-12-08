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
#include "app.h"
#include "command.h"
#include "inputwidget.h"
#include "root.h"
#include "window.h"

#include <the_Foundation/stringarray.h>
#include <SDL_timer.h>

static int initialRepeatDelayMs_ = 250;
static int repeatDelayMs_        = 75;

iDeclareType(KeyPage);
iDeclareType(KeyRow);
iDeclareType(Key);

enum iKeyFlags {
    pressed_KeyFlag = iBit(1),
    expand_KeyFlag  = iBit(2),
    spacer_KeyFlag  = iBit(3), /* half-wide */
};

struct Impl_Key {
    int      flags;
    iChar    keySym;
    iString *label;
    int      pageId;
    iRect    rect; /* relative to widget */
};

static void init_Key(iKey *d, int flags) {
    iZap(*d);
    d->flags = flags;
}

static void deinit_Key(iKey *d) {
    delete_String(d->label);
}

struct Impl_KeyRow {
    iArray keys;
};

static void init_KeyRow(iKeyRow *d) {
    init_Array(&d->keys, sizeof(iKey));
}

static void deinit_KeyRow(iKeyRow *d) {
    iForEach(Array, i, &d->keys) {
        deinit_Key(i.value);
    }
    deinit_Array(&d->keys);
}

struct Impl_KeyPage {
    int id;
    iArray rows;
};

static void init_KeyPage(iKeyPage *d) {
    init_Array(&d->rows, sizeof(iKeyRow));
}

static void deinit_KeyPage(iKeyPage *d) {
    iForEach(Array, i, &d->rows) {
        deinit_KeyRow(i.value);
    }
    deinit_Array(&d->rows);
}

struct Impl_KeyboardWidget {
    iWidget     widget;
    int         pageId; /* visible page ID */
    SDL_TimerID repeatTimer;
    int         height; /* depends on screen dimensions, key layout */
    iArray      pages;
};

iDefineObjectConstruction(KeyboardWidget);

static const char *defaultKeyboardConfig_ =
    "@us-english: U.S. English\n"

    "page: lowercase\n"
    "row: 1 2 3 4 5 6 7 8 9 0\n"
    "row: q w e r t y u i o p\n"
    "row: a s d f g h j k l\n"
    "row: {@uppercase " shift_Icon "} {} z x c v b n m {} {0xb " delete_Icon "}\n"
    "row: {@symbols #+=} {+0x20} {0xa " return_Icon "}\n"

    "page: uppercase\n"
    "row: 1 2 3 4 5 6 7 8 9 0\n"
    "row: Q W E R T Y U I O P\n"
    "row: A S D F G H J K L\n"
    "row: {@lowercase " shift_Icon "} {} Z X C V B N M {} {0xb " delete_Icon "}\n"
    "row: {@symbols #+=} {+0x20} {0xa " return_Icon "}\n"

    "page: symbols\n"
    "row: [ ] {{ } # % ^ * + =\n"
    "row: - / : ; ( ) $ & @ \"\n"
    "row: _ \\ | ~ < > € £ ¥ •\n"
    "row: . , ? ! ' {0xb " delete_Icon "}\n"
    "row: {@lowercase abc} {+0x20} {0xa " return_Icon "}\n"
    ;

static void clear_KeyboardWidget_(iKeyboardWidget *d) {
    iForEach(Array, i, &d->pages) {
        deinit_KeyPage(i.value);
    }
    clear_Array(&d->pages);
}

static int pageId_(iStringArray *names, iRangecc name) {
    iConstForEach(StringArray, i, names) {
        if (equalRange_Rangecc(name, range_String(i.value))) {
            return index_StringArrayConstIterator(&i);
        }
    }
    pushBackRange_StringArray(names, name);
    return (int) size_StringArray(names) - 1;
}

static iBool parseConfig_KeyboardWidget_(iKeyboardWidget *d, const char *config) {
    iStringArray *pageNames = new_StringArray();
    iRangecc      lineSeg   = iNullRange;
    iKeyPage     *loadPage  = NULL;
    while (nextSplit_Rangecc(range_CStr(config), "\n", &lineSeg)) {
        iRangecc line = lineSeg;
        trim_Rangecc(&line);
        if (isEmpty_Range(&line) || line.start[0] == '#') {
            continue;
        }
        if (line.start[0] == '@') {
            /* This starts a new layout. We could have a parameter for selecting which
               layout will be applied if there are multiple. Also, we could remember
               the name of the layout. */
            clear_KeyboardWidget_(d);
            loadPage = NULL;
            continue;
        }
        if (startsWith_Rangecc(line, "page:")) {
            iRangecc name = { line.start + 5, line.end };
            trimStart_Rangecc(&name);
            const int pageId = pageId_(pageNames, name);
            while (pageId >= size_Array(&d->pages)) {
                iKeyPage pad;
                init_KeyPage(&pad);
                pushBack_Array(&d->pages, &pad);
            }
            loadPage = at_Array(&d->pages, pageId);
        }
        else if (loadPage && startsWith_Rangecc(line, "row:")) {
            /* Add a new row. */
            iKeyRow row;
            init_KeyRow(&row);
            line.start += 4;
            trimStart_Rangecc(&line);
            while (!isEmpty_Range(&line)) {
                char c = line.start[0];
                if (c == '{' && size_Range(&line) >= 2) {
                    if (line.start[1] == '{') {
                        line.start++; /* double brace is used as escape */
                    }
                    else {
                        /* Special reference or symbol. */
                        iRangecc symbol = { line.start + 1, line.start + 1 };
                        while (symbol.end < line.end && *symbol.end != '}') {
                            symbol.end++;
                        }
                        trim_Rangecc(&symbol);
                        if (isEmpty_Range(&symbol)) {
                            /* A special spacer key. */
                            iKey key;
                            init_Key(&key, spacer_KeyFlag);
                            pushBack_Array(&row.keys, &key);
                        }
                        else {
                            int flags = 0;
                            if (*symbol.start == '+') {
                                flags |= expand_KeyFlag;
                                symbol.start++;
                            }
                            iKey key;
                            init_Key(&key, flags);
                            if (startsWith_Rangecc(symbol, "0x")) {
                                /* Numeric character. */
                                char *end;
                                key.keySym = strtoul(symbol.start + 2, &end, 16);
                                symbol.start = end;
                            }
                            else if (startsWith_Rangecc(symbol, "@")) {
                                /* Page reference. */
                                symbol.start++;
                                trim_Rangecc(&symbol);
                                iRangecc name = { symbol.start, symbol.start };
                                while (name.end < symbol.end && !isSpace_Char(*name.end)) {
                                    name.end++;
                                }
                                key.pageId = pageId_(pageNames, name);
                                symbol.start = name.end;
                            }
                            trimStart_Rangecc(&symbol);
                            /* Code point is followed by the label string. */
                            key.label = newRange_String(symbol);
                            pushBack_Array(&row.keys, &key);
                        }
                        line.start = symbol.end + 1;
                        continue;
                    }
                }
                iRangecc label = { line.start, line.start };
                while (label.end < line.end && !isSpace_Char(*label.end)) {
                    label.end++;
                }
                iKey key;
                init_Key(&key, 0);
                key.label = newRange_String(label);
                pushBack_Array(&row.keys, &key);
                /* Move onto the next label. */
                line.start = label.end;
                trimStart_Rangecc(&line);
            }
            pushBack_Array(&loadPage->rows, &row);
        }
    }
    iRelease(pageNames);
    return iTrue;
}

static void updateHeight_KeyboardWidget_(iKeyboardWidget *d) {
    iWidget  *w         = as_Widget(d);
    const int maxHeight = size_Root(as_Widget(d)->root).y / 2;
    const int maxRows   = 5;
    int       rowHeight = maxHeight / maxRows;
    int       numRows   = 5; /* TODO: check from current page! */
    d->height           = numRows * rowHeight;
    setFixedSize_Widget(w, init_I2(-1, d->height));
    if (!isVisible_Widget(d)) {
        setVisualOffset_Widget(w, d->height, 0, 0);
    }
    arrange_Widget(w);
    refresh_Widget(w);
}

static void showOrHide_KeyboardWidget_(iKeyboardWidget *d, iBool show) {
    iWidget *w = &d->widget;
    if (show && !isVisible_Widget(d)) {
        setVisualOffset_Widget(w, 0, 400, easeOut_AnimFlag | softer_AnimFlag);
        setFlags_Widget(w, hidden_WidgetFlag, iFalse);
    }
    else if (!show && isVisible_Widget(d)) {
        setVisualOffset_Widget(w, d->height, 400, easeOut_AnimFlag | softer_AnimFlag);
        setFlags_Widget(w, hidden_WidgetFlag, iTrue);
    }
    setKeyboardHeight_MainWindow(as_MainWindow(window_Widget(w)), show ? d->height : 0);
}

static void draw_KeyboardWidget_(const iKeyboardWidget *d) {
    const iWidget *w = &d->widget;
    draw_Widget(w);

}

static iBool processEvent_KeyboardWidget_(iKeyboardWidget *d, const SDL_Event *event) {
    if (isCommand_SDLEvent(event)) {
        const char *cmd = command_UserEvent(event);
        if (equal_Command(cmd, "focus.gained") || equal_Command(cmd, "focus.lost")) {
            showOrHide_KeyboardWidget(
                d, focus_Widget() && isInstance_Object(focus_Widget(), &Class_InputWidget));
            return iFalse;
        }
        else if (equal_Command(cmd, "keyboard.hide")) {
            showOrHide_KeyboardWidget(d, iFalse);
            return iTrue;
        }
    }
    return processEvent_Widget(&d->widget, event);
}

/*-----------------------------------------------------------------------------------------------*/

void init_KeyboardWidget(iKeyboardWidget *d) {
    iWidget *w = &d->widget;
    init_Widget(w);
    d->repeatTimer = 0;
    d->height = 50 * gap_UI;
    setBackgroundColor_Widget(w, uiBackground_ColorId);
    setFlags_Widget(w,
                    hidden_WidgetFlag | fixedHeight_WidgetFlag | resizeToParentWidth_WidgetFlag |
                        moveToParentBottomEdge_WidgetFlag | keepOnTop_WidgetFlag |
                        noFadeBackground_WidgetFlag | borderTop_WidgetFlag,
                    iTrue);
    init_Array(&d->pages, sizeof(iKeyPage));
    parseConfig_KeyboardWidget_(d, defaultKeyboardConfig_);
}

void deinit_KeyboardWidget(iKeyboardWidget *d) {
    if (d->repeatTimer) {
        SDL_RemoveTimer(d->repeatTimer);
    }
}

void showOrHide_KeyboardWidget(iKeyboardWidget *d, iBool show) {
    iWidget *w = &d->widget;
    if (show) {
        updateHeight_KeyboardWidget_(d);
    }
    showOrHide_KeyboardWidget_(d, show);
    if (show) {
        /* The keyboard must always be on top of everything else. */
        //changeChildIndex_Widget(parent_Widget(d), d, childCount_Widget(parent_Widget(d)) - 1);
    }
}

iBeginDefineSubclass(KeyboardWidget, Widget)
    .draw         = (iAny *) draw_KeyboardWidget_,
    .processEvent = (iAny *) processEvent_KeyboardWidget_,
iEndDefineSubclass(KeyboardWidget)
