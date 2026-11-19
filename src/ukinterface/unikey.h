// -*- coding:unix; mode:c++; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-
/*------------------------------------------------------------------------------
UniKey - Open-source Vietnamese Keyboard
Copyright (C) 2000-2005 Pham Kim Long
Contact:
  unikey@gmail.com
  http://unikey.org

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

#ifndef __UNIKEY_H
#define __UNIKEY_H

#include "keycons.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct UnikeySession UnikeySession;

typedef struct {
    unsigned char bytes[1024];
    int size;
    int backspaces;
} UnikeyEdit;

UnikeySession *UnikeyCreate(void);
void UnikeyDestroy(UnikeySession *session);

void UnikeyResetBuf(UnikeySession *session);
void UnikeyPutChar(UnikeySession *session, unsigned int ch);
int UnikeyAtWordBeginning(UnikeySession *session);
void UnikeySetSingleMode(UnikeySession *session);

const UnikeyEdit *UnikeyFilter(UnikeySession *session, unsigned int ch,
                               int shiftPressed, int capsLockOn);
const UnikeyEdit *UnikeyBackspacePress(UnikeySession *session);
const UnikeyEdit *UnikeyRestoreKeyStrokes(UnikeySession *session);
int UnikeyIsRevisionKey(UnikeySession *session, unsigned int key);
int UnikeyIsWordLetter(unsigned int unicode);
int UnikeyReviseWord(UnikeySession *session, const unsigned int *word, int length,
                     unsigned int key, char *output, int capacity);

void CreateDefaultUnikeyOptions(UnikeyOptions *options);
void UnikeySetOptions(UnikeySession *session, const UnikeyOptions *options);
void UnikeySetInputMethod(UnikeySession *session, UkInputMethod method);
int UnikeyLoadMacroTable(UnikeySession *session, const char *fileName);
int UnikeyLoadUserKeyMap(UnikeySession *session, const char *fileName);

#if defined(__cplusplus)
}
#endif

#endif
