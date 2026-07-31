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

/* Cach xu ly client khai bao input-purpose TERMINAL. */
enum {
  UkTerminalOff,       /* tat han go tieng Viet */
  UkTerminalPreedit    /* van go, dung preedit */
};

int UkParseOptFile(const char *fileName, UkXimOpt *options);
int UkWriteOptFile(const char *fileName, UkXimOpt *options);
char *UkGetDefConfFileName();
void UkSetDefOptions(UkXimOpt *options);
void UkTestDefConfFile();

#endif
