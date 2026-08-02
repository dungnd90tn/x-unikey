// -*- coding:unix; mode:c++; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-
/* Unikey Vietnamese Input Method
 * Copyright (C) 2004 Pham Kim Long
 * Contact:
 *   longcz@yahoo.com
 *   UniKey project: http://unikey.sf.net
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pwd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "optparse.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

#define OPT_COMMENT_CHAR '#'

//--------------------------------------------------
static int parseLine(char *line, char **name, char **value)
{
  char *p, *end, *equal;

  if (line == 0)
    return 0;

  // get rid of comment
  p = strchr(line, OPT_COMMENT_CHAR);
  if (p)
    *p = 0;

  /* Ten option va gia tri duoc phep co khoang trang o hai dau. */
  for (p = line; *p && isspace((unsigned char)*p); p++);
  if (*p == 0)
    return 0;

  *name = p;
  equal = strchr(p, '=');
  if (!equal)
    return 0;
  end = equal;
  while (end > p && isspace((unsigned char)end[-1]))
    end--;
  if (end == p)
    return 0;
  *end = 0;

  p = equal + 1;
  while (*p && isspace((unsigned char)*p))
    p++;
  if (*p == 0)
    return 0;

  *value = p;
  end = p + strlen(p);
  while (end > p && isspace((unsigned char)end[-1]))
    end--;
  *end = 0;
  return 1;
}

//----------------------------------------------------
static int parseValue(OptItem *info, void *rec, const char *strValue)
{
  char *addr = ((char *)rec)+info->offset;
  switch (info->type) {
  case BoolOpt:
    {
      int v;

      if (strcasecmp(strValue, "no") == 0 ||
	 strcasecmp(strValue, "false") == 0 ||
	 strcasecmp(strValue, "0") == 0)
	v = 0;
      else
      if (strcasecmp(strValue, "yes") == 0 ||
	 strcasecmp(strValue, "true") == 0 ||
	 strcasecmp(strValue, "1") == 0)
	v = 1;
      else
	return 0;

      /* BoolOpt/LookupOpt fields are int. The old long store overwrote the
         adjacent option on 64-bit platforms. */
      *(int *)addr = v;
    }
    break;
  case StrOpt:
    {
      char **ppStr = (char **)addr;
      char *newValue = strdup(strValue);
      if (!newValue)
	return 0;
      if (*ppStr)
	free(*ppStr);
      *ppStr = newValue;
    }
    break;
  case LookupOpt:
    {
      long v;
      OptMap *p;
      for (p = info->lookup; p->name; p++) {
	if (strcasecmp(p->name, strValue) == 0) {
	  v = p->value;
	  break;
	}
      }
      if (!p->name)
	return 0;
      *(int *)addr = (int)v;
    }
    break;
  case LongOpt:
  default:
    *(long *)addr = strtol(strValue, 0, 0);
  }

  return 1;
}

//----------------------------------------------------
int ParseOptFile(const char *fileName, void *optRec, OptItem *optList, int count)
{
  FILE *f;
  char *buf, *name, *value;
  size_t bufSize;
  ssize_t len;
  int i, ok = 1;

  if (!fileName || !optRec || !optList || count < 0)
    return 0;

  f = fopen(fileName, "r");
  if (f == 0) {
    fprintf(stderr, "Failed to open file: %s\n", fileName);
    return 0;
  }

  bufSize = 0;
  buf = NULL;

  while ((len = getline(&buf, &bufSize, f)) >= 0) {
    if (len > 0 && buf[len-1] == '\n')
      buf[--len] = 0;
    if (len > 0 && buf[len-1] == '\r')
      buf[--len] = 0;
    if (parseLine(buf, &name, &value)) {
      for (i=0; i<count; i++) {
	if (strcasecmp(optList[i].name, name) == 0) {
	  if (!parseValue(&optList[i], optRec, value))
	    ok = 0;
	  break;
	}
      }
    }
  }
  if (ferror(f))
    ok = 0;
  free(buf);
  if (fclose(f) != 0)
    ok = 0;
  return ok;
}

//----------------------------------------------------
int ParseExpandFileName(const char *name, char **expandedName)
{
  char *tmp, *path;
  const char *homeDir;
  struct passwd *user;
  size_t homeLen, pathLen;

  if (!expandedName)
    return 0;
  *expandedName = NULL;

  if (name == 0 || name[0] != '~')
    return 0;

  tmp = strdup(name);
  if (!tmp)
    return 0;

  if (tmp[1] == '/') {
    homeDir = getenv("HOME");
    if (!homeDir || !*homeDir) {
      user = getpwuid(getuid());
      homeDir = (user && user->pw_dir && *user->pw_dir) ? user->pw_dir : NULL;
    }
    path = tmp+1;
  }
  else if (tmp[1] == 0) {
    homeDir = getenv("HOME");
    if (!homeDir || !*homeDir) {
      user = getpwuid(getuid());
      homeDir = (user && user->pw_dir && *user->pw_dir) ? user->pw_dir : NULL;
    }
    path = "";
  }
  else {
    char *p = strchr(tmp+1, '/');
  
    if (p) 
      *p = 0;
    user = getpwnam(tmp+1);
    
    if (user)
      homeDir = user->pw_dir;
    else {
      free(tmp);
      return 0;
    }

    if (p) {
      *p = '/'; //restore previously overwritten slash
      path = p;
    }
    else path="";
  }

  if (!homeDir) {
    free(tmp);
    return 0;
  }

  homeLen = strlen(homeDir);
  pathLen = strlen(path);
  if (homeLen > (size_t)-1 - pathLen - 1) {
    free(tmp);
    return 0;
  }
  *expandedName = (char *)malloc(homeLen + pathLen + 1);
  if (!*expandedName) {
    free(tmp);
    return 0;
  }

  memcpy(*expandedName, homeDir, homeLen);
  memcpy(*expandedName + homeLen, path, pathLen + 1);
  free(tmp);
  return 1;
}

//----------------------------------------------------
static void writeValue(FILE *f, OptItem *optInfo, void *rec)
{
  void *addr = ((char *)rec)+optInfo->offset;

  if (optInfo->comment)
    fputs(optInfo->comment, f);

  switch (optInfo->type) {

  case BoolOpt:
    fprintf(f, "%s = %s\n\n", 
	    optInfo->name, 
	    (*(int *)addr) ? "Yes" : "No");
    break;

  case StrOpt:
    if (*(char **)addr)
      fprintf(f, "%s = %s\n\n", optInfo->name,
	     *(char **)addr);
    else {
      //      fprintf(stderr, "strange\n");
      fprintf(f, "#%s = \n\n", optInfo->name);
    }

    break;

  case LookupOpt:
    {
      OptMap *p;
      long v = *(int *)addr;

      for (p = optInfo->lookup; p->name && p->value != v; p++);
      if (p->name)
	fprintf(f, "%s = %s\n\n", optInfo->name, p->name);
    }
    break;
  case LongOpt:
  default:
    fprintf(f, "%s = %ld\n\n", optInfo->name, *(long *)addr);
  }
}

//----------------------------------------------------
static int fsyncParentDir(const char *fileName)
{
  char *dir, *slash;
  int fd, ok;

  dir = strdup(fileName);
  if (!dir)
    return 0;
  slash = strrchr(dir, '/');
  if (!slash) {
    free(dir);
    dir = strdup(".");
    if (!dir)
      return 0;
  }
  else if (slash == dir)
    slash[1] = 0;
  else
    *slash = 0;

  fd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  free(dir);
  if (fd < 0)
    return 0;
  ok = fsync(fd) == 0;
  if (close(fd) != 0)
    ok = 0;
  return ok;
}

//----------------------------------------------------
int ParseWriteOptFile(const char *fileName, const char *header,
		    void *optRec, OptItem *optList, int count)
{
  char *tmpName;
  size_t tmpLen;
  struct stat st;
  FILE *f;
  int fd, i, ok = 1;

  if (!fileName || !*fileName || !optRec || !optList || count < 0)
    return 0;

  tmpLen = strlen(fileName) + sizeof(".tmp.XXXXXX");
  tmpName = (char *)malloc(tmpLen);
  if (!tmpName)
    return 0;
  snprintf(tmpName, tmpLen, "%s.tmp.XXXXXX", fileName);

  fd = mkstemp(tmpName);
  if (fd < 0) {
    free(tmpName);
    return 0;
  }

  /* Giu quyen cua file cu; file moi cua nguoi dung mac dinh la 0600. */
  if (stat(fileName, &st) == 0 && fchmod(fd, st.st_mode & 0777) != 0)
    ok = 0;

  f = fdopen(fd, "w");
  if (!f) {
    close(fd);
    unlink(tmpName);
    free(tmpName);
    return 0;
  }

  if (header)
    fputs(header, f);
  for (i=0; i<count; i++) {
    writeValue(f, &optList[i], optRec);
  }

  if (ferror(f) || fflush(f) != 0 || fsync(fileno(f)) != 0)
    ok = 0;
  if (fclose(f) != 0)
    ok = 0;
  if (ok) {
    if (rename(tmpName, fileName) != 0)
      ok = 0;
    else
      (void)fsyncParentDir(fileName);
  }
  if (!ok)
    unlink(tmpName);
  free(tmpName);
  return ok;
}
