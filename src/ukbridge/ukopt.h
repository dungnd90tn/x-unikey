// -*- coding:unix; mode:c++; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-
#ifndef __UNIKEY_OPT_H
#define __UNIKEY_OPT_H

#include "unikey.h"

typedef struct _UkXimOpt {
  UnikeyOptions uk;
  long inputMethod;
  long charset;
  long enabled;
  long bellNotify;
  long autoSave;
  char *macroFile;
  char *usrKeyMapFile;
  long terminalMode;
} UkXimOpt;

/* Lam gi trong o nhap khong ho tro surrounding text (dien hinh la
   gnome-terminal). Xem chu thich TerminalModeCmt trong ukopt.c. */
enum {
  UkTerminalOff,       /* tat han go tieng Viet */
  UkTerminalPreedit    /* van go, dung preedit gach chan */
};

int UkParseOptFile(const char *fileName, UkXimOpt *options);
int UkWriteOptFile(const char *fileName, UkXimOpt *options);
char *UkGetDefConfFileName();
void UkSetDefOptions(UkXimOpt *options);
void UkTestDefConfFile();

#endif
