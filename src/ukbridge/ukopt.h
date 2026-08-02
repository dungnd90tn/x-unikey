// -*- coding:unix; mode:c++; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-
#ifndef __UNIKEY_OPT_H
#define __UNIKEY_OPT_H

#include "unikey.h"

typedef struct _UkXimOpt {
  UnikeyOptions uk;
  int inputMethod;
  int charset;
  int enabled;
  int bellNotify;
  int autoSave;
  char *macroFile;
  char *usrKeyMapFile;
  int terminalMode;
} UkXimOpt;

/* Cach xu ly client khai bao input-purpose TERMINAL. */
enum {
  UkTerminalOff,       /* tat han go tieng Viet */
  UkTerminalPreedit    /* van go, dung preedit */
};

int UkParseOptFile(const char *fileName, UkXimOpt *options);
int UkWriteOptFile(const char *fileName, UkXimOpt *options);
int UkWriteOptFileAtomic(const char *fileName, UkXimOpt *options);
char *UkGetDefConfFileName(void);
void UkSetDefOptions(UkXimOpt *options);
int UkTestDefConfFile(void);

#endif
