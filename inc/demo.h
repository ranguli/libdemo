/***************************************************************************
 *
 * Copyright (c) 2013-2015 Mathias Thore
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

#ifndef DEMO_H
#define DEMO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>

/*****************************************************************************
 *                                                                           *
 *                DATA TYPES                                                 *
 *                                                                           *
 *****************************************************************************/

/* Portability
 *
 * _DEMO_LITTLE_ENDIAN/_DEMO_BIG_ENDIAN
 * The library performs endian conversion on 16 and 32 bit
 * numbers to be able to parse the demo data. Define the
 * macro that describes your architecture.
 */
#if defined __i386__
#define _DEMO_LITTLE_ENDIAN
#elif defined __x86_64
#define _DEMO_LITTLE_ENDIAN
#elif defined __ARM_EABI__
#define _DEMO_LITTLE_ENDIAN
#elif defined __PPC__
#define _DEMO_BIG_ENDIAN
#elif defined __mips
#define _DEMO_BIG_ENDIAN
#else
#error unsupported architecture
#endif

/* Demos are represented by the demo data type, pointing to a linked list of
 * blocks, each in turn pointing to a linked list of messages.
 */

typedef struct _message {
  uint32_t size;
  uint32_t type;
  uint8_t *data;
  struct _message *next;
  struct _message *prev;
} message;

typedef struct _block {
  uint32_t length;
  float angles[3];
  message *messages;
  struct _block *next;
  struct _block *prev;
} block;

typedef struct _demo {
  uint32_t protocol;
  int32_t track;
  block *blocks;
} demo;

typedef struct _flagfield {
  void *flag;
  void *value;
} flagfield;

/* Return data type
 */
typedef int dret_t;

/* Progress callback function type
 */
typedef void (*progress_cb_t)(unsigned int);

/*****************************************************************************
 *                                                                           *
 *                DEFINES                                                    *
 *                                                                           *
 *****************************************************************************/

#if defined _DEMO_LITTLE_ENDIAN
#define htods(a) (a)
#define htodl(a) (a)
#define dtohs
#define dtohl
#elif defined _DEMO_BIG_ENDIAN
#define htods(a) ((((uint16_t)(a) & 0x00FF) << 8) |             \
                  (((uint16_t)(a) & 0xFF00) >> 8))
#define htodl(a) ((((uint32_t)(a) & 0x000000FF) << 24) |        \
                  (((uint32_t)(a) & 0x0000FF00) << 8)  |        \
                  (((uint32_t)(a) & 0x00FF0000) >> 8)  |        \
                  (((uint32_t)(a) & 0xFF000000) >> 24))
#define dtohs htods
#define dtohl htodl
#else
#error machine endian not supplied
#endif

/*****************************************************************************
 *                                                                           *
 *                API                                                        *
 *                                                                           *
 *****************************************************************************/

/**
 * @function demo_read
 *
 * @input flags Tag - value array describing the desired operation,
 *              constructed out of READFLAG* tags.
 *
 * @input demo  Where to write a pointer to the read demo.
 *
 * @return DEMO_OK upon success. The read demo will be returned through
 *         the demo pointer. Upon failure, an error code will be returned,
 *         and the demo pointer will remain unchanged.
 *
 * @long Reads a quake demo file from a supplied file name or FILE pointer,
 *       and returns it for processing.
 */
extern dret_t demo_read(flagfield *flags, demo **demo);

/**
 * @function demo_write
 *
 * @input flags Tag - value array describing the desired operation,
 *              constructed out of WRITEFLAG* tags.
 *
 * @input demo  The demo to be written to file.
 *
 * @return DEMO_OK upon success. Upon failure, an error code will be
 *         returned, and the possibly partly written file might be unplayable.
 *
 * @long Writes quake demo data pointed to by the demo pointer to a file.
 */
extern dret_t demo_write(flagfield *flags, demo *demo);

/**
 * @function demo_free
 *
 * @input demo The demo to free.
 *
 * @return DEMO_OK.
 *
 * @long Frees the resources (memory) used to describe the demo, including
 *       the demo itself.
 */
extern dret_t demo_free(demo *demo);

/**
 * @function demo_free_data
 *
 * @input demo The demo to free.
 *
 * @return DEMO_OK.
 *
 * @long Frees the resources (memory) used to describe the demo. All block
 *       and message data is freed, but the demo itself is not.
 */
extern dret_t demo_free_data(demo *demo);

/**
 * @function demo_error
 *
 * @input errcode Error code returned from any of the other demo API
 *                functions.
 *
 * @return Pointer to a static string describing the error that has
 *         happened.
 *
 * @long Translates demo error codes to human readable error strings.
 */
extern char *demo_error(dret_t errcode);

/*****************************************************************************
 *                                                                           *
 *                FLAGS                                                      *
 *                                                                           *
 *****************************************************************************/

/*****************************************************************************
 *                READ FLAGS                                                 *
 *****************************************************************************/

#define READFLAG_FILENAME        (void *)100
#define READFLAG_FP              (void *)101
#define READFLAG_PROGRESS_CB     (void *)102
#define READFLAG_END             (void *)800

/*****************************************************************************
 *                WRITE FLAGS                                                *
 *****************************************************************************/

#define WRITEFLAG_FILENAME       (void *)200
#define WRITEFLAG_FP             (void *)201
#define WRITEFLAG_REPLACE        (void *)202
#define WRITEFLAG_END            (void *)800

/*****************************************************************************
 *                                                                           *
 *                RETURN VALUES                                              *
 *                                                                           *
 *****************************************************************************/

#define DEMO_OK                  0
#define DEMO_CANNOT_OPEN_DEMO    1
#define DEMO_CORRUPT_DEMO        3
#define DEMO_FILE_EXISTS         4
#define DEMO_CANNOT_WRITE        5
#define DEMO_UNKNOWN_PROTOCOL    6
#define DEMO_UNEXPECTED_EOF      7
#define DEMO_BAD_PARAMS          8
#define DEMO_NO_MEMORY           9
#define DEMO_INTERNAL_1          50

#define DEMO_BAD_FILE            DEMO_CORRUPT_DEMO // obsolete

/*****************************************************************************
 *                                                                           *
 *                DEMO PROTOCOLS                                             *
 *                                                                           *
 *****************************************************************************/

#define PROTOCOL_UNKNOWN         0
#define PROTOCOL_NETQUAKE        15
#define PROTOCOL_FITZQUAKE       666
#define PROTOCOL_BJP3            10002

/*****************************************************************************
 *                                                                           *
 *                DEMO MESSAGE TYPES                                         *
 *                                                                           *
 *****************************************************************************/

#define BAD                      0x00
#define NOP                      0x01
#define DISCONNECT               0x02
#define UPDATESTAT               0x03
#define VERSION                  0x04
#define SETVIEW                  0x05
#define SOUND                    0x06
#define TIME                     0x07
#define PRINT                    0x08
#define STUFFTEXT                0x09
#define SETANGLE                 0x0A
#define SERVERINFO               0x0B
#define LIGHTSTYLE               0x0C
#define UPDATENAME               0x0D
#define UPDATEFRAGS              0x0E
#define CLIENTDATA               0x0F
#define STOPSOUND                0x10
#define UPDATECOLORS             0x11
#define PARTICLE                 0x12
#define DAMAGE                   0x13
#define SPAWNSTATIC              0x14
#define SPAWNBINARY              0x15
#define SPAWNBASELINE            0x16
#define TEMP_ENTITY              0x17
#define SETPAUSE                 0x18
#define SIGNONUM                 0x19
#define CENTERPRINT              0x1A
#define KILLEDMONSTER            0x1B
#define FOUNDSECRET              0x1C
#define SPAWNSTATICSOUND         0x1D
#define INTERMISSION             0x1E
#define FINALE                   0x1F
#define CDTRACK                  0x20
#define SELLSCREEN               0x21
#define CUTSCENE                 0x22
//PROTOCOL_FITZQUAKE
#define FQSKYBOX                 0x25
#define FQBF                     0x28
#define FQFOG                    0x29
#define FQSPAWNBASELINE2         0x2A
#define FQSPAWNSTATIC2           0x2B
#define FQSPAWNSTATICSOUND2      0x2C
//PROTOCOL_BJP3
#define BJP3SHOWLMP              0x23
#define BJP3HIDELMP              0x24
#define BJP3SKYBOX               0x25
#define BJP3FOG                  0x33


#ifdef __cplusplus
}
#endif

#endif // DEMO_H
