// -*- coding:unix; mode:c++; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-
/*------------------------------------------------------------------------------
UniKey - Open-source Vietnamese Keyboard
Copyright (C) 1998-2004 Pham Kim Long
Contact:
  longcz@yahoo.com
  http://unikey.sf.net

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
--------------------------------------------------------------------------------*/
#include <new>
#include "unikey.h"
#include "ukengine.h"
#include "usrkeymap.h"

struct UnikeySession {
    UkSharedMem control;
    UkEngine engine;
    UnikeyEdit edit;

    UnikeySession()
    {
        control.input.init();
        control.macStore.init();
        control.vietKey = 1;
        control.usrKeyMapLoaded = 0;
        control.charsetId = CONV_CHARSET_XUTF8;
        control.initialized = 1;
        CreateDefaultUnikeyOptions(&control.options);
        engine.setCtrlInfo(&control);
    }
};

UnikeySession *UnikeyCreate()
{
    return new (std::nothrow) UnikeySession;
}

void UnikeyDestroy(UnikeySession *session)
{
    delete session;
}

void CreateDefaultUnikeyOptions(UnikeyOptions *options)
{
    *options = {};
    options->freeMarking = 1;
    options->spellCheckEnabled = 1;
}

void UnikeySetOptions(UnikeySession *session, const UnikeyOptions *options)
{
    session->control.options = *options;
}

void UnikeySetInputMethod(UnikeySession *session, UkInputMethod method)
{
    if (method == UkTelex || method == UkVni || method == UkViqr) {
        session->control.input.setIM(method);
        session->engine.reset();
    } else if (method == UkUsrIM && session->control.usrKeyMapLoaded) {
        session->control.input.setIM(session->control.usrKeyMap);
        session->engine.reset();
    }
}

const UnikeyEdit *UnikeyFilter(UnikeySession *session, unsigned int ch,
                               int shiftPressed, int capsLockOn)
{
    UnikeyEdit &edit = session->edit;
    UkOutputType type;
    edit.size = sizeof(edit.bytes);
    session->engine.setKeyboardCase(shiftPressed, capsLockOn);
    session->engine.process(ch, edit.backspaces, edit.bytes, edit.size, type);
    return &edit;
}

void UnikeyPutChar(UnikeySession *session, unsigned int ch)
{
    session->engine.pass(ch);
}

void UnikeyResetBuf(UnikeySession *session)
{
    session->engine.reset();
}

void UnikeySetSingleMode(UnikeySession *session)
{
    session->engine.setSingleMode();
}

int UnikeyAtWordBeginning(UnikeySession *session)
{
    return session->engine.atWordBeginning();
}

const UnikeyEdit *UnikeyBackspacePress(UnikeySession *session)
{
    UnikeyEdit &edit = session->edit;
    UkOutputType type;
    edit.size = sizeof(edit.bytes);
    session->engine.processBackspace(edit.backspaces, edit.bytes, edit.size, type);
    return &edit;
}

int UnikeyLoadMacroTable(UnikeySession *session, const char *fileName)
{
    return session->control.macStore.loadFromFile(fileName);
}

int UnikeyLoadUserKeyMap(UnikeySession *session, const char *fileName)
{
    if (!UkLoadKeyMap(fileName, session->control.usrKeyMap))
        return 0;
    session->control.usrKeyMapLoaded = 1;
    return 1;
}

const UnikeyEdit *UnikeyRestoreKeyStrokes(UnikeySession *session)
{
    UnikeyEdit &edit = session->edit;
    UkOutputType type;
    edit.size = sizeof(edit.bytes);
    session->engine.restoreKeyStrokes(edit.backspaces, edit.bytes, edit.size, type);
    return &edit;
}

int UnikeyIsWordLetter(unsigned int unicode)
{
    return UnikeyUnicodeLetter(unicode) != vnl_nonVnChar;
}

int UnikeyIsRevisionKey(UnikeySession *session, unsigned int key)
{
    return session->engine.isRevisionKey(key);
}

int UnikeyReviseWord(UnikeySession *session, const unsigned int *word, int length,
                     unsigned int key, char *output, int capacity)
{
    UkEngine editor;
    editor.setCtrlInfo(&session->control);
    int size = capacity - 1;
    if (!editor.reviseWord(word, length, key, reinterpret_cast<unsigned char *>(output), size))
        return 0;
    output[size] = '\0';
    return size;
}
