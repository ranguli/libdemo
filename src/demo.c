#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <setjmp.h>

#include "demo.h"

/*****************************************************************************
 *                                                                           *
 *                DEFINITIONS                                                *
 *                                                                           *
 *****************************************************************************/

#define MAX_BLOCK_LENGTH 65536 // from lmpc
#define CB_BLOCKS (72*30) // make callbacks every n blocks

#define DEMO_PROTOCOL_NOT_PRESENT DEMO_INTERNAL_1

#define GET_MEMORY(ptr, size, ret, label) do {  \
  ptr = calloc(1, (size));                      \
  if (ptr == NULL) {                            \
    ret = DEMO_NO_MEMORY;                       \
    goto label;                                 \
  }                                             \
} while(0)

/*****************************************************************************
 *                                                                           *
 *                DATA TYPES                                                 *
 *                                                                           *
 *****************************************************************************/

/* Metadata structure used during demo opening
 */
typedef struct {
  FILE *fp;
  uint32_t protocol;
  progress_cb_t pcb;
  jmp_buf jumpbuf;
  uint8_t buffer[MAX_BLOCK_LENGTH];
  uint8_t buffer2[2048];
} deminfo;

/*****************************************************************************
 *                                                                           *
 *                FORWARD REFERENCES                                         *
 *                                                                           *
 *****************************************************************************/

static int read_demo_data(deminfo *di, demo **dem);
static int read_blocks(deminfo *di, block **b);
static int read_block(deminfo *di, block **br);
static int read_messages(deminfo *di, message **m, uint32_t length);
static int read_message(deminfo *di, message **mr);
static int read_cdtrack(deminfo *di, int32_t *track);
static int read_string(deminfo *di, char *target);
static float read_float(deminfo *di);
static uint32_t read_uint32_t(deminfo *di);
static uint16_t read_uint16_t(deminfo *di);
static uint8_t read_uint8_t(deminfo *di);
static void read_n_uint8_t(deminfo *di, int n, uint8_t *buf);

static int write_demo_data(FILE *fp, demo *demo);
static int write_blocks(FILE *fp, block *bp);
static int write_block(FILE *fp, block *b);
static int write_messages(FILE *fp, message *m, uint32_t length);
static int write_message(FILE *fp, message *m, size_t *written);
static size_t write_uint32_t(FILE *fp, uint32_t du32);
static size_t write_float(FILE *fp, float df32);

static int free_blocks(block *b);
static int free_block(block *b);
static int free_messages(message *m);
static int free_message(message *m);

static char *msg_name(deminfo *di, int type);
static int fpeek(FILE *fp);
static int find_protocol(message *m, uint32_t *p);
static int count_setbits(uint32_t mask);

/*****************************************************************************
 *                                                                           *
 *                API                                                        *
 *                                                                           *
 *****************************************************************************/

char *demo_error(int errcode)
{
  printf("demo_error was called with errcode %d", &errcode);

  switch (errcode) {
  case DEMO_OK:
    return "no error";

  case DEMO_CANNOT_OPEN_DEMO:
    return "cannot open file";

  case DEMO_CORRUPT_DEMO:
    return "corrupt demo";

  case DEMO_FILE_EXISTS:
    return "demo file exists";

  case DEMO_CANNOT_WRITE:
    return "cannot write demo data to file";

  case DEMO_UNKNOWN_PROTOCOL:
    return "demo has unknown protocol";

  case DEMO_UNEXPECTED_EOF:
    return "demo file ended unexpectedly";

  case DEMO_BAD_PARAMS:
    return "invalid parameters supplied";

  case DEMO_NO_MEMORY:
    return "memory allocation failed";

  default:
    return "unknown demo error";
  }
}

/*****************************************************************************
 *                READ API                                                   *
 *****************************************************************************/

// What the heck is this?
static inline int bp(int code)
{
  return code;
}

/* Reads a quake demo file from a file pointer. Constructs a package of metainfo about the demo
 * which is passed onto read_demo_data() to help it do the heavy lifting.
 * 
 * Args:
 * 	- flagfield *flags - Value array describing the desired operation, constructed out of
 *        readflag tags.
 *      - demo **dem - Where the write a pointer to the read demo.
 *
 * Returns: 
 * 	DEMO_OK upon success. The read demo will be returned through the demo pointer.
 * 	Upon failure, an error code will be returned, and the demo pointer will remain unchanged.
 */
int demo_read(flagfield *flags, demo **dem)
{

  deminfo *di;
  int ret;
  FILE *local_fp = NULL;

  GET_MEMORY(di, sizeof(deminfo), ret, demo_read_failure);
  di->protocol = PROTOCOL_UNKNOWN;

  // The flags, of type flagfield, 
  if (flags == NULL) {
    ret = DEMO_BAD_PARAMS;
    goto demo_read_failure;
  }

  // Parse the readflags the user supplied to the function until we reach th end

  while (flags->flag != READFLAG_END) {
    switch ((size_t) flags->flag) {
    case (size_t) READFLAG_FILENAME:
      if (di->fp != NULL) {
        ret = DEMO_BAD_PARAMS;
        goto demo_read_failure;
      }

      local_fp = fopen((char *) flags->value, "rb");
	
      // If local_fp is still NULL after that fopen(), something is wrong
      if (local_fp == NULL) {
        ret = DEMO_BAD_PARAMS;
        goto demo_read_failure;
      }
      di->fp = local_fp;
      break;

    case (size_t) READFLAG_FP:
      if (di->fp != NULL) {
        ret = DEMO_BAD_PARAMS;
        goto demo_read_failure;
      }
      di->fp = (FILE *) flags->value;
      break;

    case (size_t) READFLAG_PROGRESS_CB:
      di->pcb = (progress_cb_t) flags->value;
      break;

    default:
      ret = DEMO_BAD_PARAMS;
      goto demo_read_failure;
    }
    flags++;
  }

  if (di->fp == NULL) {
    ret = DEMO_CANNOT_OPEN_DEMO;
    goto demo_read_failure;
  }

  // Read the demo. di now contains a file pointer, a protocol (UNKNOWN, currently), 
  // all the other variables the struct contains by default.
  ret = read_demo_data(di, dem);

 demo_read_failure:
  if (local_fp != NULL) {
    fclose(local_fp);
  }
  if (di != NULL) {
    free(di);
  }
  return ret;
}

/*****************************************************************************
 *                WRITE API                                                  *
 *****************************************************************************/

int demo_write(flagfield *flags, demo *demo)
{
  char *filename = NULL;
  FILE *fp = NULL;
  FILE *local_fp = NULL;
  int ret;
  int replace = 0;

  if (flags == NULL) {
    ret = DEMO_BAD_PARAMS;
    goto demo_write_failure;
  }

  while (flags->flag != WRITEFLAG_END) {
    switch ((size_t) flags->flag) {
    case (size_t) WRITEFLAG_FILENAME:
      if (fp != NULL || filename != NULL) {
        ret = DEMO_BAD_PARAMS;
        goto demo_write_failure;
      }
      filename = (char *) flags->value;
      break;

    case (size_t) WRITEFLAG_FP:
      if (fp != NULL || filename != NULL) {
        ret = DEMO_BAD_PARAMS;
        goto demo_write_failure;
      }
      fp = (FILE *) flags->value;
      break;

    case (size_t) WRITEFLAG_REPLACE:
      replace = 1;
      break;

    default:
      ret = DEMO_BAD_PARAMS;
      goto demo_write_failure;
    }
    flags++;
  }

  // we need either a file name or fp
  if (fp == NULL && filename == NULL) {
      ret = DEMO_BAD_PARAMS;
      goto demo_write_failure;
  }

  // open file locally?
  if (filename != NULL) {
    if (replace == 0) {
      local_fp = fopen(filename, "r");
      if (local_fp != NULL) {
        fclose(local_fp);
        ret = DEMO_FILE_EXISTS;
        goto demo_write_failure;
      }
    }
    local_fp = fopen (filename, "wb");
    if (local_fp == NULL) {
      ret = DEMO_CANNOT_OPEN_DEMO;
      goto demo_write_failure;
    }
    fp = local_fp;
  }

  // write the demo
  ret = write_demo_data(fp, demo);

 demo_write_failure:
  if (local_fp != NULL) {
    fclose(local_fp);
  }
  return ret;
}

/*****************************************************************************
 *                FREE API                                                   *
 *****************************************************************************/

int demo_free(demo *d)
{
  if (d != NULL) {
    free_blocks(d->blocks);
    free(d);
  }

  return DEMO_OK;
}

int demo_free_data(demo *d)
{
  if (d) {
    free_blocks(d->blocks);
    d->blocks = NULL;
  }

  return DEMO_OK;
}

/*****************************************************************************
 *                                                                           *
 *                READ FUNCTIONS                                             *
 *                                                                           *
 *****************************************************************************/

/* Reads the data within a demo, using meta information about it determined by demo_read()
 * and stored in *di. 
 */
static int read_demo_data(deminfo *di, demo **dem)
{

  demo *d;
  block *b;
  int ret;
  int32_t cdtrack = 0;

  // alloc demo
  GET_MEMORY(d, sizeof(demo), ret, read_demo_data_failure);

  // read cd track
  ret = read_cdtrack(di, &cdtrack);
  if (ret != DEMO_OK) {
    goto read_demo_data_failure;
  }
  d->track = cdtrack;
  
  // read all the blocks, into b
  ret = read_blocks(di, &b);
  if (ret != DEMO_OK) {
    goto read_demo_data_failure;
  }

  d->blocks = b;
  d->protocol = di->protocol;

  // return demo
  *dem = d;
  return DEMO_OK;

 read_demo_data_failure:
  demo_free(d);
  return ret;
}

/* Demo files are made up of blocks of data, which stored in a linked list, as the blocks
 * are sequentially describing the events of a gameplay recording. This method, given a deminfo struct
 * containing meta-information about a demo, and a place to store the block, iterates over all of the blocks
 * in a demo file, with the heavy lifting of parsing each individual block being sent off to read_block()
 */
static int read_blocks(deminfo *di, block **b)
{
  block *head = NULL;
  block *lastblock;
  block *newblock = NULL;
  int ret;
  int cb_c = 0;

  // Iterate until EOF of the demo file pointer
  while (!feof(di->fp)) {
    if (fpeek(di->fp) == EOF) {
      break;
    }

    // read a block
    ret = read_block(di, &newblock);
    if (ret != DEMO_OK) {
      goto read_blocks_failure;
    }

    // insert into linked list
    if (head == NULL) {
      head = newblock;
      newblock->prev = NULL;
    }
    else {
      lastblock->next = newblock;
      newblock->prev = lastblock;
    }
    lastblock = newblock;

    // progress callback?
    if (di->pcb != NULL) {
      if (cb_c++ > CB_BLOCKS) {
        cb_c = 0;
        di->pcb(ftell(di->fp));
      }
    }
  }

  *b = head;
  return DEMO_OK;

 read_blocks_failure:
  free_blocks(head);
  return ret;
}

/* Each block is made of a size value, a 3D vector (x,y,z) describing the camera viewing direction, and the remaining bytes
 * make up one or more 'messages'. Given a single block and a deminfo struct, this function gets the block size, vector, and the
 * messages 
 */
static int read_block(deminfo *di, block **br)
{
  block *b;
  message *m;
  uint32_t length;
  int ret;
 
  // This is again likely some kind of error handling
  ret = setjmp(di->jumpbuf);
  if (ret != 0) {
    goto read_block_failure;
  }

  // The size value makes up the first 32bits of the block.
  length = read_uint32_t(di);
  if (length > MAX_BLOCK_LENGTH) {
    return bp(DEMO_CORRUPT_DEMO);
  }

  // alloc the needed memory
  GET_MEMORY(b, sizeof(block), ret, read_block_failure);
  b->length = length;

  // Next in the binary block are three 32bit floating point numbers that make up the 
  // 3D vector representing the camera view direction
  // printf("(%2.f,%2.f,%2.f)\n", b->angles[0], b->angles[1], b->angles[2]);

  b->angles[0] = read_float(di);
  b->angles[1] = read_float(di);
  b->angles[2] = read_float(di);


  // Any remaining data in the block are one or more messages.
  ret = read_messages(di, &m, length);
  if (ret != DEMO_OK) {
    goto read_block_failure;
  }
  b->messages = m;

  // return demo
  *br = b;
  return DEMO_OK;

 read_block_failure:
  bp(0);
  free_block(b);
  return ret;
}

/* At the end of a block are one or more messages. Their size is variable depending on the type and contents
 * of the message.
 */
static int read_messages(deminfo *di, message **m, uint32_t length)
{
  uint32_t protocol;
  uint32_t messagelen = 0;
  message *head = NULL;
  message *lastmessage;
  message *newmessage;
  int ret;

  do {
    ret = read_message(di, &newmessage);
    if (ret != DEMO_OK) {
      goto read_messages_failure;
    }
    messagelen += newmessage->size + 1; // +1 because of type byte

    if (head == NULL) {
      head = newmessage;
      newmessage->prev = NULL;
    }
    else {
      lastmessage->next = newmessage;
      newmessage->prev = lastmessage;
    }
    lastmessage = newmessage;

    // find demo protocol
    if (di->protocol == PROTOCOL_UNKNOWN) {
      ret = find_protocol(newmessage, &protocol);
      switch (ret) {
      case DEMO_OK:
        di->protocol = protocol;
        break;

      case DEMO_UNKNOWN_PROTOCOL:
        goto read_messages_failure;
        break;

      default:
        break;
      }
    }
  } while (messagelen < length);

  // error check, we expect an exact amount of data
  if (messagelen != length) {
    ret = bp(DEMO_CORRUPT_DEMO);
    goto read_messages_failure;
  }

  // return messages
  *m = head;
  return DEMO_OK;

 read_messages_failure:
  bp(0);
  free_messages(head);
  return ret;
}

/** Read an invidual message.
 *
 */ 
static int read_message(deminfo *di, message **mr)
{
  int process = 0;
  message *m;
  uint32_t mask;
  int i;
  int ret;

  // Weird error handling
  ret = setjmp(di->jumpbuf);
  if (ret != 0) {
    goto read_message_failure;
  }

  GET_MEMORY(m, sizeof(message), ret, read_message_failure);

  // The first 8 bits of the message describes its type, of which there are many.
  // Knowing the type will help determine the size of the following message contents
  // we are to read.
  m->type = read_uint8_t(di);

  // These are all defined as hex values in the header file. This switch statement 
  // lays out for us what the expected message size yieled by each type will be.
  switch (m->type) {
  case BAD:
  case NOP:
  case DISCONNECT:
  case SPAWNBINARY:
  case KILLEDMONSTER:
  case FOUNDSECRET:
  case INTERMISSION:
  case SELLSCREEN:
    m->size = 0;
    break;
  case SETPAUSE:
  case SIGNONUM:
    m->size = 1;
    break;
  case SETVIEW:
  case STOPSOUND:
  case UPDATECOLORS:
  case CDTRACK:
    m->size = 2;
    break;
  case SETANGLE:
  case UPDATEFRAGS:
    m->size = 3;
    break;
  case VERSION:
  case TIME:
    m->size = 4;
    break;
  case UPDATESTAT:
    m->size = 5;
    break;
  case DAMAGE:
    m->size = 8;
    break;
  case SPAWNSTATICSOUND:
    m->size = 9;
    break;
  case PARTICLE:
    m->size = 11;
    break;
  case SPAWNSTATIC:
    m->size = 13;
    break;
  case SPAWNBASELINE:
    m->size = 15;
    break;

  default: // variable size messages or non standard protocol
    process = 1;
  }

  // This appears to be something specific to Fitzquake engines using something called the
  // FitzQuake protocol. It seems to implement extra message types on top of the pre-existing ones.
  // Cool!

  if (di->protocol == PROTOCOL_FITZQUAKE && process == 1) {
    switch (m->type) {
    case FQBF:
      process = 0;
      m->size = 0;
      break;

    case FQFOG:
      process = 0;
      m->size = 6;
      break;

    case FQSPAWNSTATICSOUND2:
      process = 0;
      m->size = 10;
      break;

    case FQSKYBOX:
    case FQSPAWNBASELINE2:
    case FQSPAWNSTATIC2:
      break;
    }
  }
	
  // I think this is the Nehahra BJP3 protocol? That's what google says anyway. Similar deal to Fitzquake.
  // Actually the quake wiki indicates that there are tons of additional protocols that are supersets of the
  // original.  
  if (di->protocol == PROTOCOL_BJP3) {
    switch (m->type) {
    case SPAWNBASELINE:
    case SPAWNSTATIC:
      m->size += 1;
      break;

    case SPAWNSTATICSOUND:
      // size change nulled by Compatibility flag
      break;

    default:
      break;
    }
  }

  if (!process) {
    // deal with messages with known length
    GET_MEMORY(m->data, m->size, ret, read_message_failure);

    // Read n (m->size) integers, 8bits in size, from deminfo into the m->data buffer.
    read_n_uint8_t(di, m->size, m->data);
  }
  else {
    // deal with messages with unknown length
    switch (m->type) {
    case PRINT:
    case STUFFTEXT:
    case CENTERPRINT:
    case FINALE:
    case CUTSCENE:
      {
        // it's a string
        m->size = read_string(di, di->buffer);
        GET_MEMORY(m->data, m->size, ret, read_message_failure);
        memcpy(m->data, di->buffer, m->size);
      }
      break;

    case FQSKYBOX:
  //case BJP3SKYBOX:
      if (di->protocol == PROTOCOL_FITZQUAKE ||
          di->protocol == PROTOCOL_BJP3)
      {
        // it's a string
        m->size = read_string(di, di->buffer);
        GET_MEMORY(m->data, m->size, ret, read_message_failure);
        memcpy(m->data, di->buffer, m->size);
      }
      else {
        ret = bp(DEMO_CORRUPT_DEMO);
        goto read_message_failure;
      }
      break;

    case FQSPAWNBASELINE2:
      if (di->protocol == PROTOCOL_FITZQUAKE) {
        uint8_t entnum1;
        uint8_t entnum2;

        m->size = 15 + 1; // +1 for flag byte

        entnum1 = read_uint8_t(di); // entnum precedes the mask
        entnum2 = read_uint8_t(di);
        mask = read_uint8_t(di); // the flag byte
        if (mask & 0x01) {
          m->size += 1;
        }
        if (mask & 0x02) {
          m->size += 1;
        }
        if (mask & 0x04) {
          m->size += 1;
        }
        GET_MEMORY(m->data, m->size, ret, read_message_failure);
        m->data[0] = entnum1;
        m->data[1] = entnum2;
        m->data[2] = (uint8_t) mask;
        read_n_uint8_t(di, m->size - 3, m->data + 3);
      }
      else {
        ret = bp(DEMO_CORRUPT_DEMO);
        goto read_message_failure;
      }
      break;

    case FQSPAWNSTATIC2:
      if (di->protocol == PROTOCOL_FITZQUAKE) {
        m->size = 13 + 1; // +1 for flag byte

        mask = read_uint8_t(di); // the flag byte
        if (mask & 0x01) {
          m->size += 1;
        }
        if (mask & 0x02) {
          m->size += 1;
        }
        if (mask & 0x04) {
          m->size += 1;
        }
        GET_MEMORY(m->data, m->size, ret, read_message_failure);
        m->data[0] = (uint8_t) mask;
        read_n_uint8_t(di, m->size - 1, m->data + 1);
      }
      else {
        ret = bp(DEMO_CORRUPT_DEMO);
        goto read_message_failure;
      }
      break;

    case SOUND:
      {
        mask = read_uint8_t(di); // the flag byte
        m->size = 10;
        if (di->protocol == PROTOCOL_BJP3) {
          m->size += 1; // sound_num short rather than byte
        }
        if (mask & 0x01) {
          m->size += 1;
        }
        if (mask & 0x02) {
          m->size += 1;
        }
        if (di->protocol == PROTOCOL_FITZQUAKE) {
          if (mask & 0x08) {
            m->size += 1;
          }
          if (mask & 0x10) {
            m->size += 1;
          }
        }
        GET_MEMORY(m->data, m->size, ret, read_message_failure);
        m->data[0] = (uint8_t) mask;
        read_n_uint8_t(di, m->size - 1, m->data + 1);
      }
      break;

    case SERVERINFO:
      {
        int size;
        m->size = 6;
        read_n_uint8_t(di, 6, di->buffer);

        // force read map title (there may be none)
        size = read_string(di, di->buffer2);
        memcpy(di->buffer + m->size, di->buffer2, size);
        m->size += size;

        // read mapname and models
        do {
          size = read_string(di, di->buffer2);
          memcpy(di->buffer + m->size, di->buffer2, size);
          m->size += size;
        } while (size > 1);

        // read sounds
        do {
          size = read_string(di, di->buffer2);
          memcpy(di->buffer + m->size, di->buffer2, size);
          m->size += size;
        } while (size > 1);

        // copy to exact sized buffer
        GET_MEMORY(m->data, m->size, ret, read_message_failure);
        memcpy(m->data, di->buffer, m->size);
      }
      break;

    case LIGHTSTYLE:
    case UPDATENAME:
      {
        i = read_uint8_t(di);
        m->size = read_string(di, di->buffer) + 1;
        GET_MEMORY(m->data, m->size, ret, read_message_failure);
        m->data[0] = (uint8_t) i;
        memcpy(m->data + 1, di->buffer, m->size - 1);
      }
      break;

    case CLIENTDATA:
      {
        uint16_t mask16;
        uint8_t du8;
        uint8_t extramask1;
        uint8_t extramask2;
        uint32_t bytemask;
        m->size = 14; // 14 bytes minimum

        mask16 = read_uint16_t(di);
        mask = mask16;
        if (di->protocol == PROTOCOL_FITZQUAKE) {
          if (mask & 0x8000) {
            m->size += 1;
            extramask1 = read_uint8_t(di);
            mask |= (extramask1 << 16);
            if (mask & 0x00800000) {
              m->size += 1;
              extramask2 = read_uint8_t(di);
              mask |= (extramask2 << 24);
            }
          }
        }

        // each of these bits cost an additional 1 byte
        if (di->protocol == PROTOCOL_FITZQUAKE) {
          bytemask = 0x37F70FF;
        }
        else {
          bytemask = 0x70FF;
        }
        m->size += count_setbits(mask & bytemask);
        if (di->protocol == PROTOCOL_BJP3) {
          if (mask & (0x4000)) {
            m->size += 1; // SU_WEAPON short rather than byte
          }
        }

        if (mask & 0x80000000) {
          ret = bp(DEMO_CORRUPT_DEMO);
          goto read_message_failure; // unsupported
        }

        i = 0;
        GET_MEMORY(m->data, m->size, ret, read_message_failure);
        du8 = (mask16 & 0x00FF);
        m->data[i++] = du8;
        du8 = (mask16 & 0xFF00) >> 8;
        m->data[i++] = du8;

        if (di->protocol == PROTOCOL_FITZQUAKE) {
          if (mask & 0x8000) {
            m->data[i++] = extramask1;
          }
          if (mask & 0x00800000) {
            m->data[i++] = extramask2;
          }
        }
        read_n_uint8_t(di, m->size - i, m->data + i);
      }
      break;

    case TEMP_ENTITY:
      {
        uint8_t type;
        type = read_uint8_t(di);
        switch (type) {
        case 0: case 1: case 2: case 3: case 4:
        case 7: case 8: case 10: case 11:
          m->size = 7;
          break;

        case 5: case 6: case 9: case 13:
          m->size = 15;
          break;

        case 12:
          m->size = 9;
          break;
        }
        GET_MEMORY(m->data, m->size, ret, read_message_failure);
        m->data[0] = type;
        read_n_uint8_t(di, m->size - 1, m->data + 1);
      }
      break;

    case BJP3SHOWLMP:
      if (di->protocol == PROTOCOL_BJP3) {
        char slotname[2048];
        size_t slotname_l;
        char lmpfilename[2048];
        size_t lmpfilename_l;

        // [string] slotname [string] lmpfilename [coord] x [coord] y
        slotname_l = read_string(di, slotname);
        lmpfilename_l = read_string(di, lmpfilename);
        m->size = slotname_l + lmpfilename_l + 2;
        GET_MEMORY(m->data, m->size, ret, read_message_failure);
        memcpy(m->data, slotname, slotname_l);
        memcpy(m->data + slotname_l, lmpfilename, lmpfilename_l);
        m->data[m->size - 2] = read_uint8_t(di);
        m->data[m->size - 1] = read_uint8_t(di);
      }
      else {
        ret = bp(DEMO_CORRUPT_DEMO);
        goto read_message_failure;
      }
      break;

    case BJP3HIDELMP:
      if (di->protocol == PROTOCOL_BJP3) {
        // [string] slotname
        m->size = read_string(di, di->buffer);
        GET_MEMORY(m->data, m->size, ret, read_message_failure);
        memcpy(m->data, di->buffer, m->size);
      }
      else {
        ret = bp(DEMO_CORRUPT_DEMO);
        goto read_message_failure;
      }
      break;

    case BJP3FOG:
      if (di->protocol == PROTOCOL_BJP3) {
        uint8_t enable;

        // [byte] enable
        // <optional past this point, only included if enable is true>
        // [float] density [byte] red [byte] green [byte] blue
        m->size = 1;
        enable = read_uint8_t(di);
        if (enable) {
          m->size += 7;
        }
        GET_MEMORY(m->data, m->size, ret, read_message_failure);
        m->data[0] = enable;
        if (enable) {
          read_n_uint8_t(di, 7, m->data + 1);
        }
      }
      else {
        ret = bp(DEMO_CORRUPT_DEMO);
        goto read_message_failure;
      }
      break;

    default:
      // an entity update
      if ((m->type & 128) == 0) {
        ret = bp(DEMO_CORRUPT_DEMO);
        goto read_message_failure;
      }
      else {
        uint8_t extramask1;
        uint8_t extramask2;
        uint8_t extramask3;
        uint32_t bytemask;

        mask = m->type & 0x7F;

        m->size = 1;
        if (mask & 0x01) {
          m->size += 1;
          extramask1 = read_uint8_t(di);
          mask |= (extramask1 << 8);
        }

        if (di->protocol == PROTOCOL_FITZQUAKE) {
          if (mask & 0x8000) {
            m->size += 1;
            extramask2 = read_uint8_t(di);
            mask |= (extramask2 << 16);
          }
          if (mask & 0x800000) {
            m->size += 1;
            extramask3 = read_uint8_t(di);
            mask |= (extramask3 << 24);
          }
        }

        // each of these bits cost an additional 1 byte
        if (di->protocol == PROTOCOL_FITZQUAKE) {
          bytemask = 0xF7F50;
        }
        else {
          bytemask = 0x7F50;
        }
        m->size += count_setbits(mask & bytemask);

        // these bits cost an additional 2 bytes
        m->size += count_setbits(mask & 0xE) << 1;

        // this bit may cost an additional byte
        if (di->protocol == PROTOCOL_BJP3) {
          if (mask & (0x0400)) {
            m->size += 1; // U_MODEL short rather than byte
          }
        }

        GET_MEMORY(m->data, m->size, ret, read_message_failure);

        i = 0;
        if (mask & 0x01) {
          m->data[i++] = extramask1;
        }
        if (di->protocol == PROTOCOL_FITZQUAKE) {
          if (mask & 0x8000) {
            m->data[i++] = extramask2;
          }
          if (mask & 0x800000) {
            m->data[i++] = extramask3;
          }
        }
        read_n_uint8_t(di, m->size - i, m->data + i);
      }
      break;
    }
  }
  //printf("ok the type of the message is %x i think with size %x\n", m->type, m->size);

  *mr = m;
  return DEMO_OK;

 read_message_failure:
  bp(0);
  free_message(m);
  return ret;
}

/* Read the next 8bit integer from the deminfo file stream.
 *
 */
static uint8_t read_uint8_t(deminfo *di)
{
  int n;
  int n2;
  n = getc(di->fp);
  
  if (n == EOF) {
    longjmp(di->jumpbuf, DEMO_UNEXPECTED_EOF);
  }
  // I'm not totally sure what this bitwise AND accomplishes. n is an unsigned int, and we 
  // bitwise AND it with a hexadecimal value (255). 
  return (uint8_t) (n & 0x000000FF);
}

/* Read an arbitrary (n) amount of unsigned 8bit integers from deminfo into the buffer, buf. 
 * This is basically a wrapper for fread() which operates in a very similar mindset. 
 */
static void read_n_uint8_t(deminfo *di, int n, uint8_t *buf)
{
  size_t count;

  if (n == 0) {
    return;
  }
  
  // Read one element, n bytes in size, from the stream di->fp, into the buffer at buf 
  // buf. fread() autoincrements the seek position.
  count = fread(buf, n, 1, di->fp);
  
  // Weird error handling again.
  if (count != 1) {
    longjmp(di->jumpbuf, DEMO_UNEXPECTED_EOF);
  }
}

static uint16_t read_uint16_t(deminfo *di)
{
  uint16_t i;

  read_n_uint8_t(di, 2, (uint8_t *)&i);
  return i;
}

static uint32_t read_uint32_t(deminfo *di)
{
  uint32_t i;

  read_n_uint8_t(di, 4, (uint8_t *)&i);
  return i;
}

static float read_float(deminfo *di)
{
  uint32_t i = read_uint32_t(di);
  return *(float *)&i;
}

static int read_string(deminfo *di, char *target)
{
  char *string_pointer;
  int i;

  string_pointer = target;
  for (i = 0; i < 0x7FF; i++, string_pointer++) {
    if (!(*string_pointer = read_uint8_t(di))) {
      break;
    }
  }
  *string_pointer = '\0';
  return i + 1;
}

/* Parses the demo file to determine what CD track
 * (from the Quake soundtrack) is set to be played
 */
static int read_cdtrack(deminfo *di, int32_t *track)
{

  int32_t cdtrack = 0;
  int number = 0;
  int sign = 0;
  int readcount = 0;
  int ret;
  
  // Remove strang jump error handling in Rust rewrite.
  ret = setjmp(di->jumpbuf);
  if (ret != 0) {
    return ret;
  }

  // Sequentially read unsigned 8bit ints from deminfo struct until newline
  while ((number = read_uint8_t(di)) != '\n') {
    printf("Reading number from di, got: %d \t %x\n", number, number);

    // This has something to do with comparing signed to unsigned integers, I think..
    if (number == '-') {
      printf("number == '-', so sign = 1\n");
      sign = 1;
    }
    else {
      number -= '0'; // Surely the point of this is related to the above ^ 
      if (number > 9) {
        return bp(DEMO_CORRUPT_DEMO);
      }
      cdtrack = cdtrack * 10 + number;
    }
    if (++readcount > 6) {
      // don't expect more than 6 chars to select a cd track
      // ^ tbh expect the unexpected
      return bp(DEMO_CORRUPT_DEMO);
    }
  }
  if (sign) {
    cdtrack = -cdtrack;
  }

  // Throughout my findings, the CD track will be -1 if there was no music on during the demo recording. 
  // Likely because the actual CD tracklisting starts at 0.
  *track = cdtrack;
  return DEMO_OK;
}

/*****************************************************************************
 *                                                                           *
 *                WRITE FUNCTIONS                                            *
 *                                                                           *
 *****************************************************************************/

static int write_demo_data(FILE *fp, demo *demo)
{
  uint32_t writesize;

  // write cd track
  writesize = fprintf(fp, "%d\n", demo->track);
  if (writesize < 2) {
    return DEMO_CANNOT_WRITE;
  }

  return write_blocks(fp, demo->blocks);
}

static int write_blocks(FILE *fp, block *bp)
{
  block *b;
  int ret;

  for (b = bp; b != NULL; b = b->next) {
    if (b->length == 0) {
      continue;
    }
    ret = write_block(fp, b);
    if (ret != DEMO_OK) {
      return ret;
    }
  }

  return DEMO_OK;
}

static int write_block(FILE *fp, block *b)
{
  size_t count;
  int i;

  // write length
  count = write_uint32_t(fp, b->length);
  if (count != 1) {
    return DEMO_CANNOT_WRITE;
  }

  // write angles
  for (i = 0; i < 3; i++) {
    count = write_float(fp, b->angles[i]);
    if (count != 1) {
      return DEMO_CANNOT_WRITE;
    }
  }

  return write_messages(fp, b->messages, b->length);
}

static int write_messages(FILE *fp, message *m, uint32_t length)
{
  int ret;
  size_t written;
  uint32_t writesize;

  // write all block messages
  writesize = 0;
  for (; m != NULL; m = m->next) {
    ret = write_message(fp, m, &written);
    if (ret != DEMO_OK) {
      return ret;
    }

    writesize += written;
    if (writesize > length) {
      return bp(DEMO_CORRUPT_DEMO);
    }
  }

  // validate demo integrity
  if (writesize != length) {
    return bp(DEMO_CORRUPT_DEMO);
  }

  return DEMO_OK;
}

static int write_message(FILE *fp, message *m, size_t *written)
{
  uint8_t du8;
  size_t w = 0;
  int count;

  // write message id
  if (m->type > UCHAR_MAX) {
    return bp(DEMO_CORRUPT_DEMO);
  }
  du8 = (uint8_t)m->type;
  count = fwrite(&du8, sizeof(du8), 1, fp);
  if (count != 1) {
    return DEMO_CANNOT_WRITE;
  }
  w += 1;

  // write message data
  if (m->size != 0) {
    count = fwrite(m->data, m->size, 1, fp);
    if (count != 1) {
      return DEMO_CANNOT_WRITE;
    }
  }
  w += m->size;

  *written = w;
  return DEMO_OK;
}

static size_t write_uint32_t(FILE *fp, uint32_t du32)
{
  uint8_t du[4];

  du[0] = (du32 & 0x000000FF);
  du[1] = (du32 & 0x0000FF00) >> 8;
  du[2] = (du32 & 0x00FF0000) >> 16;
  du[3] = (du32 & 0xFF000000) >> 24;
  return fwrite(du, sizeof(du), 1, fp);
}

static size_t write_float(FILE *fp, float df32)
{
  uint32_t du32 = *(uint32_t *)&df32;
  return write_uint32_t(fp, du32);
}

/*****************************************************************************
 *                                                                           *
 *                FREE FUNCTIONS                                             *
 *                                                                           *
 *****************************************************************************/

static int free_blocks(block *b)
{
  block *bnext;

  for (; b != NULL; b = bnext) {
    bnext = b->next;
    free_block(b);
  }

  return DEMO_OK;
}

static int free_block(block *b)
{
  if (b) {
    free_messages(b->messages);
    free(b);
  }

  return DEMO_OK;
}

static int free_messages(message *m)
{
  message *mnext;

  for (; m != NULL; m = mnext) {
    mnext = m->next;
    free_message(m);
  }

  return DEMO_OK;
}

static int free_message(message *m)
{
  if (m) {
    if (m->data) {
      free(m->data);
    }
    free(m);
  }
}

/*****************************************************************************
 *                                                                           *
 *                HELPER FUNCTIONS                                           *
 *                                                                           *
 *****************************************************************************/

static int fpeek(FILE *fp)
{
  int c;

  c = fgetc(fp);
  ungetc(c, fp);
  return c;
}

static int find_protocol(message *m, uint32_t *p)
{
  uint32_t protocol = PROTOCOL_UNKNOWN;

  if (m->type == SERVERINFO || m->type == VERSION) {
    protocol  = m->data[0];
    protocol |= m->data[1] << 8;
    protocol |= m->data[2] << 16;
    protocol |= m->data[3] << 24;

    *p = protocol;
    if (protocol != PROTOCOL_NETQUAKE &&
        protocol != PROTOCOL_FITZQUAKE &&
        protocol != PROTOCOL_BJP3)
    {
      return DEMO_UNKNOWN_PROTOCOL;
    }
    else {
      return DEMO_OK;
    }
  }

  return DEMO_PROTOCOL_NOT_PRESENT;
}

static int count_setbits(uint32_t mask)
{
  int count;
  for (count = 0; mask; count++) {
    mask &= mask - 1;
  }
  return count;
}

static char *msg_name(deminfo *di, int type)
{
#define UNSUP "unsupported message"
  static char *msg_names[] = {
    "bad",
    "nop",
    "disconnect",
    "updatestat",
    "version",
    "setview",
    "sound",
    "time",
    "print",
    "stufftext",
    "setangle",
    "serverinfo",
    "lightstyle",
    "updatename",
    "updatefrags",
    "clientdata",
    "stopsound",
    "updatecolors",
    "particle",
    "damage",
    "spawnstatic",
    "spawnbinary",
    "spawnbaseline",
    "temp_entity",
    "setpause",
    "signonnum",
    "centerprint",
    "killedmonster",
    "founsecret",
    "spawnstaticsound",
    "intermission",
    "finale",
    "cdtrack",
    "sellscreen",
    "cutscene",
  };
  static char *fq_msg_names[] = {
    "skybox (fq)",
    UNSUP,
    UNSUP,
    "bf (fq)",
    "fog (fq)",
    "spawnbaseline2 (fq)",
    "spawnstatic2 (fq)",
    "spawnstaticsound2 (fq)",
  };
  static char *bjp3_msg_names[] = {
    "showlmp (bjp3)",
    "hidelmp (bjp3)",
    "skybox (bjp3)",
    UNSUP,
    UNSUP,
    UNSUP,
    UNSUP,
    UNSUP,
    UNSUP,
    UNSUP,
    UNSUP,
    UNSUP,
    UNSUP,
    UNSUP,
    UNSUP,
    UNSUP,
    "fog (bjp3)",
  };
  if (type >= 128) {
    return "quick update";
  }
  else if (type < sizeof(msg_names) / sizeof(msg_names[0])) {
    return msg_names[type];
  }
  else if (di->protocol == PROTOCOL_FITZQUAKE &&
           type >= FQSKYBOX &&
           type <= FQSPAWNSTATICSOUND2)
  {
    return fq_msg_names[type - FQSKYBOX];
  }
  else if (di->protocol == PROTOCOL_BJP3 &&
           type >= BJP3SHOWLMP &&
           type <= BJP3FOG)
  {
    return bjp3_msg_names[type - BJP3SHOWLMP];
  }
  else {
    return UNSUP;
  }
}
