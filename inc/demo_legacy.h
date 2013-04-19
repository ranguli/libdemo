/***************************************************************************
 *
 * Copyright (c) 2013 Mathias Thore
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 ***************************************************************************/

#ifndef DEMO_LEGACY_H
#define DEMO_LEGACY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include "demo.h"

/*****************************************************************************
 *                                                                           *
 *                LEGACY API                                                 *
 *                                                                           *
 *****************************************************************************/

static dret_t read_demo(char *filename, demo **demo)
{
  flagfield flags[] = { {READFLAG_FILENAME, filename},
                        {READFLAG_END, READFLAG_END} };

  return demo_read(flags, demo);
}

static dret_t read_demo_pcb(char *filename, demo **demo, progress_cb_t cb)
{
  flagfield flags[] = { {READFLAG_FILENAME, filename},
                        {READFLAG_PROGRESS_CB, cb},
                        {READFLAG_END, READFLAG_END} };

  return demo_read(flags, demo);
}

static dret_t write_demo(char *filename, demo *demo)
{
  flagfield flags[] = { {WRITEFLAG_FILENAME, filename},
                        {WRITEFLAG_END, WRITEFLAG_END} };

  return demo_write(flags, demo);
}

static dret_t write_demo_fp(FILE *fp, demo *demo)
{
  flagfield flags[] = { {WRITEFLAG_FP, fp},
                        {WRITEFLAG_END, WRITEFLAG_END} };

  return demo_write(flags, demo);
}

#define free_demo demo_free

#define free_demo_data demo_free_data

#ifdef __cplusplus
}
#endif

#endif // DEMO_LEGACY_H
