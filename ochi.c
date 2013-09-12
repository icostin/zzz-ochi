#include <stdio.h>
#include <c41.h>
#include <hbs1.h>
#include <acx1.h>

#if __GNUC__
#include <inttypes.h>
#else
#endif
#define NUM_WORKERS             2

#define MIN_WIDTH               40
#define MIN_HEIGHT              10

#define OV_UNKNOWN              (-1)
#define OV_NODATA               (-2)

// ochi return code
#define ORC_ACX_ERROR           0x01
#define ORC_FILE_ERROR          0x02
#define ORC_MEM_ERROR           0x04
#define ORC_REPORT_ERROR        0x08 // error reporting error
#define ORC_THREAD_ERROR        0x10
#define ORC_CLI_ERROR           0x20
#define ORC_MISC_ERROR          0x40

#define E(_eid, ...)                                                          \
  if (!(c41_u8v_afmt(&o->emsg, __VA_ARGS__) ||                                \
        c41_u8v_afmt(&o->emsg, " (code $i: $s, line $i)\n",                   \
                     (o->eid = (_eid)), ename(_eid), __LINE__)))             \
    /*printf("\nErrors:\n%s\n", o->emsg.a)*/; \
  else (o->orc |= ORC_REPORT_ERROR)

#define MLOCK() do { uint_t c;                                  \
    /*printf("locking... (line %d)\n", __LINE__);*/ \
    c = c41_smt_mutex_lock(o->smt_p, o->main_mutex_p);          \
    if (c) {                                                    \
      /*printf("lock error (line %d)...\n", __LINE__);*/ \
      E(OE_MAIN_MUTEX_LOCK, "main mutex lock error: $i", c);    \
      o->orc |= ORC_THREAD_ERROR;                               \
      goto l_thread_error;                                      \
    }                                                           \
    /*printf("locked (line %d)\n", __LINE__);*/ \
  } while (0)

#define MUNLOCK() do { uint_t c;                                \
    /*printf("unlocking (line %d)...\n", __LINE__);*/ \
    c = c41_smt_mutex_unlock(o->smt_p, o->main_mutex_p);        \
    if (c) {                                                    \
      /*printf("unlock error (line %d)...\n", __LINE__);*/ \
      E(OE_MAIN_MUTEX_UNLOCK, "main mutex unlock error: $i", c);\
      o->orc |= ORC_THREAD_ERROR;                               \
      goto l_thread_error;                                      \
    }                                                           \
    /*printf("unlocked (line %d)...\n", __LINE__);*/ \
  } while (0)

#define OWAIT() do { uint_t c;                                  \
    c = c41_smt_cond_wait(o->smt_p, o->out_cond_p, o->main_mutex_p); \
    if (c) {                                                    \
      E(OE_OUTPUT_COND_WAIT, "output cond wait error: $i", c);  \
      o->orc |= ORC_THREAD_ERROR;                               \
      goto l_thread_error;                                      \
    }                                                           \
  } while (0)

#define OSIGNAL() do { uint_t c;                                \
    c = c41_smt_cond_signal(o->smt_p, o->out_cond_p);           \
    if (c) {                                                    \
      E(OE_OUTPUT_COND_SIGNAL, "output cond signal error: $i", c);  \
      o->orc |= ORC_THREAD_ERROR;                               \
      goto l_thread_error;                                      \
    }                                                           \
  } while (0)

#define JWAIT() do { uint_t c;                                  \
    c = c41_smt_cond_wait(o->smt_p, o->job_cond_p, o->main_mutex_p); \
    if (c) {                                                    \
      E(OE_WORKER_COND_WAIT, "worker cond wait error: $i", c);  \
      o->orc |= ORC_THREAD_ERROR;                               \
      goto l_thread_error;                                      \
    }                                                           \
  } while (0)

#define JSIGNAL() do { uint_t c;                                \
    c = c41_smt_cond_signal(o->smt_p, o->job_cond_p);           \
    if (c) {                                                    \
      E(OE_WORKER_COND_SIGNAL, "worker cond signal error: $i", c);  \
      o->orc |= ORC_THREAD_ERROR;                               \
      goto l_thread_error;                                      \
    }                                                           \
  } while (0)

#define A(_x) do { uint_t c = (_x); \
    if (c) { E(OE_ACX_WRITE, "($s) failed: $s = $i", #_x, \
               acx1_status_str(c), c); \
      o->orc |= ORC_ACX_ERROR; \
      goto l_acx_error; \
    } \
  } while (0)

#define O(...) do { uint_t c; c = c41_u8v_afmt(&o->out_data, __VA_ARGS__); \
  if (c) { E(OE_RENDER_DATA, "failed (code $i)", c); return 1; } } while (0)

#define OQ_SIZE (1 << 4)
#define OQ_MASK (OQ_SIZE - 1)

#define WQ_SIZE (1 << 4)
#define WQ_MASK (WQ_SIZE - 1)


enum ochi_err_enum
{
  OE_NONE = 0,
  OE_NO_CODE,
  OE_STDOUT_ERROR,
  OE_TWO_CMD,
  OE_MISS_SH_OPT,
  OE_UNKNOWN_OPT,
  OE_BAD_CLI,
  OE_MEM_ERROR,
  OE_ACX_INIT,
  OE_ACX_READ,
  OE_ACX_WRITE,
  OE_INPUT_THREAD_CREATE,
  OE_INPUT_THREAD_JOIN,
  OE_MAIN_MUTEX_CREATE,
  OE_MAIN_MUTEX_DESTROY,
  OE_MAIN_MUTEX_LOCK,
  OE_MAIN_MUTEX_UNLOCK,
  OE_OUTPUT_COND_CREATE,
  OE_OUTPUT_COND_DESTROY,
  OE_OUTPUT_COND_WAIT,
  OE_OUTPUT_COND_SIGNAL,
  OE_OUTPUT_THREAD_CREATE,
  OE_OUTPUT_THREAD_JOIN,
  OE_WORKER_COND_CREATE,
  OE_WORKER_COND_DESTROY,
  OE_WORKER_COND_WAIT,
  OE_WORKER_COND_SIGNAL,
  OE_WORKER_THREAD_CREATE,
  OE_WORKER_THREAD_JOIN,
  OE_RENDER_TITLE,
  OE_RENDER_DATA,
  OE_FILE_CLOSE,
  OE_FILE_OPEN,
  OE_FILE_NAME,
  OE_FILE_SEEK,
};

enum ochi_clicmd_enum
{
  OCMD_NOP = 0,
  OCMD_ARG_ERROR,
  OCMD_HELP,
  OCMD_WITH_UI,
  OCMD_MAIN = OCMD_WITH_UI,
  OCMD_TEST_UI,
};

enum ochi_cmd_enum
{
  OC_SHOW_TITLE_BAR = 1, // redraw title bar
  OC_SHOW_DATA, // redraw data panel
  OC_SHOW_MSG_AREA, // redraw msg panel
  OC_SHOW_TOO_SMALL, // screen too small
  OC_READ_DATA,
};

enum ochi_mode_enum
{
  OM_HEX_ABR, // ABR = ascii byte representation
  OM_ABR,
};

enum ochi_action_enum
{
  OA_EXIT = 0,
  OA_CURSOR_LEFT,
  OA_CURSOR_DOWN,
  OA_CURSOR_UP,
  OA_CURSOR_RIGHT,
  OA_CURSOR_HIGH,
  OA_CURSOR_MED,
  OA_CURSOR_LOW,
  OA_CURSOR_GO,
  OA_CURSOR_END,
  OA_CURSOR_PAGE_UP,
  OA_CURSOR_PAGE_DOWN,
  OA_CURSOR_BLOCK_UP,
  OA_CURSOR_BLOCK_DOWN,
  OA_CURSOR_BOL,
  OA_CURSOR_EOL,
  OA_CURSOR_PERC,
  OA_DEC_WIDTH,
  OA_INC_WIDTH,
  OA_SLIDE_LEFT,
  OA_SLIDE_RIGHT,
  OA_SLIDE_UP,
  OA_SLIDE_DOWN,
};

/*
 * active | inactive
 * cursor | directional | non-directional
 * selected | non-selected
 * highlighted | non-highlighted * 4
 */
enum ochi_style_enum
{
  OS_NORMAL0 = 0,
  OS_NORMAL,
  OS_OFS_SEP,
  OS_HEX_SEP,
  OS_DHEX_SEP,
  OS_ABR_SEP,
  OS_NOFS,
  OS_DOFS,
  OS_NHEX, // normal
  OS_SHEX, // selection
  OS_HHEX, // highlight (search matches)
  OS_DHEX, // directional highlight (row/column with cursor)
  OS_CHEX, // cursor
  OS_NHEX_NODATA,
  OS_SHEX_NODATA,
  OS_HHEX_NODATA,
  OS_DHEX_NODATA,
  OS_CHEX_NODATA,
  OS_NHEX_UNK,
  OS_SHEX_UNK,
  OS_HHEX_UNK,
  OS_DHEX_UNK,
  OS_CHEX_UNK,
  OS_ABR_CONTROL,
  OS_ABR_SYMBOL,
  OS_ABR_DIGIT,
  OS_ABR_LETTER,
  OS_ABR_80_BF,
  OS_ABR_C0_FF,
  OS_ABR_UNK,
  OS_ABR_NODATA,
  OS_CABR_CONTROL,
  OS_CABR_SYMBOL,
  OS_CABR_DIGIT,
  OS_CABR_LETTER,
  OS_CABR_80_BF,
  OS_CABR_C0_FF,
  OS_CABR_UNK,
  OS_CABR_NODATA,
  OS_TB_TEXT, // title bar - static text
  OS_TB_EM, // text with emphasis
  OS_TB_NAME, // title bar - name
  OS_TB_OFS, // title bar - offset & size
  OS__COUNT
};

typedef struct ochi_s                           ochi_t;
typedef struct ochi_kap_s                       ochi_kap_t;

struct ochi_kap_s
{
  uint32_t      key;
  uint32_t      action;
};

C41_VECTOR_DECL(ochi_kapv_t, ochi_kap_t);
C41_VECTOR_FNS(ochi_kapv_t, ochi_kap_t, kapv);

struct ochi_s
{
  uint_t        cmd;
  uint8_t const * fname;
  c41_io_t *    io_p;
  int64_t       offset;
  int64_t       page_offset;
  uint_t        line_items;
  uint_t        item_row;
  uint_t        item_col;
  uint_t        msg_rows;
  uint_t        data_top; // line index of data area
  uint_t        data_rows;
  uint_t        mode;

  c41_u8v_t     data_cache[2];
  int64_t       ofs_cache[2];
  char const *  ofs_sep;
  char const *  abr_sep;
  char const *  hex_sep[4];
  ochi_kapv_t   kapv;

  acx1_attr_t   attr_a[OS__COUNT];

  c41_smt_mutex_t * main_mutex_p;
  c41_smt_cond_t * out_cond_p;
  c41_smt_cond_t * job_cond_p;

  uint8_t       oq[OQ_SIZE];
  uint8_t       oq_bx, oq_ex;

  uint8_t       wq[WQ_SIZE];
  uint8_t       wq_bx, wq_ex;

  uint8_t       adcx; // active data cache index

  c41_u8v_t     out_data;
  c41_pv_t      out_lines;
  uint16_t      out_row, out_col, out_height, out_width;

  c41_smt_tid_t input_tid;
  c41_smt_tid_t output_tid;
  c41_smt_tid_t worker_tid_a[NUM_WORKERS];
  uint_t        worker_n; // number of inited workers
  uint_t        workers_left;

  uint16_t      h, w;

  char          ofs_fmt[16];
  uint8_t       orc; // return code
  uint_t        eid; // error id
  c41_u8v_t     emsg;
  c41_ma_t *    ma_p;
  c41_smt_t *   smt_p;
  c41_fsi_t *   fsi_p;
  c41_fspi_t *  fspi_p;
  uint8_t       acx_inited;
  uint8_t       input_thread_inited;
  uint8_t       out_thread_inited;
  uint8_t       out_cond_inited;
  uint8_t       job_cond_inited;
  uint8_t       exiting;
  uint8_t       screen_too_small;

  c41_io_t *    log_io_p;
};

static char const help_msg[] =
 "ochi - data viewer\n"
 ;

uint8_t C41_CALL hmain (c41_cli_t * cli_p);
static void help (ochi_t * o, c41_io_t * io_p);
static void oq_push (ochi_t * o, uint8_t cmd);
static void wq_push (ochi_t * o, uint8_t cmd);
static uint8_t C41_CALL input_reader (void * arg);
static uint8_t C41_CALL redraw_screen (ochi_t * o); // called in unlocked state
static uint8_t C41_CALL screen_too_small (ochi_t * o);
static uint8_t C41_CALL output_writer (void * arg);
static uint8_t C41_CALL worker (void * arg);
static int set_cmd (ochi_t * o, uint_t cmd);
static void init_default_styles (ochi_t * o);
static void init (ochi_t * o, c41_cli_t * cli_p);
static void finish (ochi_t * o);
static void run_ui (ochi_t * o); // should be called with mutex locked
static void test_ui (ochi_t * o);
static void cmd_main (ochi_t * o);
static void C41_CALL screen_resized (ochi_t * o, uint16_t h, uint16_t w);
static uint8_t C41_CALL render_title (ochi_t * o);
static void prepare_offset_format (ochi_t * o);
static uint_t data_line_len (ochi_t * o, uint_t n);
static int abr_ch (int ch);
static int abr_style (int ch);
static size_t extend_cache (ochi_t * o, int index, size_t len);
static uint_t init_default_keys (ochi_t * o);
static char const * ename (uint_t eid);
static void move_cursor (ochi_t * o, int action);

/* ename ********************************************************************/
static char const * ename (uint_t eid)
{
#define N(_x) case _x: return #_x;
  switch (eid)
  {
    N(OE_NONE);
    N(OE_NO_CODE);
    N(OE_STDOUT_ERROR);
    N(OE_TWO_CMD);
    N(OE_MISS_SH_OPT);
    N(OE_UNKNOWN_OPT);
    N(OE_MEM_ERROR);
    N(OE_ACX_INIT);
    N(OE_ACX_READ);
    N(OE_ACX_WRITE);
    N(OE_INPUT_THREAD_CREATE);
    N(OE_INPUT_THREAD_JOIN);
    N(OE_MAIN_MUTEX_CREATE);
    N(OE_MAIN_MUTEX_DESTROY);
    N(OE_MAIN_MUTEX_LOCK);
    N(OE_MAIN_MUTEX_UNLOCK);
    N(OE_OUTPUT_COND_CREATE);
    N(OE_OUTPUT_COND_DESTROY);
    N(OE_OUTPUT_COND_WAIT);
    N(OE_OUTPUT_COND_SIGNAL);
    N(OE_OUTPUT_THREAD_CREATE);
    N(OE_OUTPUT_THREAD_JOIN);
    N(OE_FILE_CLOSE);
    N(OE_FILE_OPEN);
    N(OE_FILE_NAME);
    N(OE_FILE_SEEK);
    N(OE_RENDER_DATA);
    N(OE_RENDER_TITLE);
  default:
    return "THE_ERROR_WHOSE_NAME_WE_DO_NOT_PRINT";
  }
}

/* help *********************************************************************/
static void help (ochi_t * o, c41_io_t * io_p)
{
  if (c41_io_write_full(io_p, help_msg, sizeof(help_msg) - 1, NULL))
  {
    E(OE_STDOUT_ERROR, "failed printing help");
    return;
  }
}

/* oq_push ******************************************************************/
static void oq_push (ochi_t * o, uint8_t cmd)
{
  uint8_t i;
  for (i = o->oq_bx; i != o->oq_ex; i = (i + 1) & OQ_MASK)
  {
    if (o->oq[i] == cmd) return;
  }
  o->oq[i] = cmd;
  o->oq_ex = (i + 1) & OQ_MASK;
}

/* wq_push ******************************************************************/
static void wq_push (ochi_t * o, uint8_t cmd)
{
  uint8_t i;
  for (i = o->wq_bx; i != o->wq_ex; i = (i + 1) & WQ_MASK)
  {
    if (o->wq[i] == cmd) return;
  }
  o->wq[i] = cmd;
  o->wq_ex = (i + 1) & WQ_MASK;
}

/* move_cursor *****************************************************/
static void move_cursor (ochi_t * o, int action)
{
  int64_t new_pos;
  int64_t page_end;

  MLOCK();
  switch (action)
  {
  case OA_CURSOR_LEFT:
    new_pos = o->offset - 1;
    break;
  case OA_CURSOR_RIGHT:
    new_pos = o->offset + 1;
    break;
  case OA_CURSOR_UP:
    new_pos = o->offset - o->line_items;
    break;
  case OA_CURSOR_DOWN:
    new_pos = o->offset + o->line_items;
    break;
  case OA_CURSOR_HIGH:
    new_pos = o->page_offset;
    break;
  case OA_CURSOR_LOW:
    new_pos = o->page_offset + o->line_items * (o->data_rows - 1);
    break;
  case OA_CURSOR_MED:
    new_pos =  o->page_offset + o->line_items * (o->data_rows / 2);
    break;
  case OA_CURSOR_GO:
    new_pos = 0;
    break;
  case OA_CURSOR_END:
    new_pos = o->io_p->size - 1;
    break;
  case OA_CURSOR_PAGE_DOWN:
    new_pos = o->offset + (o->data_rows - 1) * o->line_items;
    o->page_offset += (o->data_rows - 1) * o->line_items;
    break;
  case OA_CURSOR_PAGE_UP:
    new_pos = o->offset - (o->data_rows - 1) * o->line_items;
    o->page_offset -= (o->data_rows - 1) * o->line_items;
    break;
  case OA_CURSOR_BLOCK_DOWN:
    new_pos = o->offset + (o->data_rows / 3) * o->line_items;
    o->page_offset += (o->data_rows - 1) * o->line_items;
    break;
  case OA_CURSOR_BLOCK_UP:
    new_pos = o->offset - (o->data_rows / 3) * o->line_items;
    o->page_offset -= (o->data_rows - 1) * o->line_items;
    break;
  case OA_CURSOR_BOL:
    new_pos = o->offset - o->item_col;
    break;
  case OA_CURSOR_EOL:
    new_pos = o->offset - o->item_col + o->line_items - 1;
    break;
  case OA_CURSOR_PERC:
    new_pos = o->io_p->size / 2;
    break;
  case OA_DEC_WIDTH:
    if (o->line_items > 1) o->line_items -= 1;
    new_pos = o->offset;
    break;
  case OA_INC_WIDTH:
    if (data_line_len(o, o->line_items + 1) <= o->w)
      o->line_items += 1;
    new_pos = o->offset;
    break;
  case OA_SLIDE_LEFT:
    new_pos = o->offset;
    o->page_offset += 1;
    break;
  case OA_SLIDE_RIGHT:
    new_pos = o->offset;
    o->page_offset -= 1;
    break;
  case OA_SLIDE_UP:
    new_pos = o->offset;
    o->page_offset -= o->line_items;
    break;
  case OA_SLIDE_DOWN:
    new_pos = o->offset;
    o->page_offset += o->line_items;
    break;
  default:
    new_pos = o->offset;
  }

  if (new_pos < o->page_offset)
  {
    o->page_offset -= o->line_items * 
      ((o->page_offset - new_pos + o->line_items - 1) / o->line_items);
  }
  else if (new_pos >= 
           (page_end = o->page_offset + o->line_items * o->data_rows))
  {
    o->page_offset += o->line_items * 
      ((new_pos + o->line_items - page_end) / o->line_items);
  }
  /* now new pos should be inside the screen */
  o->item_row = (uint_t) (new_pos - o->page_offset) / o->line_items;
  o->item_col = (uint_t) (new_pos - o->page_offset) - o->item_row * o->line_items;

  if (o->offset != new_pos) oq_push(o, OC_SHOW_TITLE_BAR);
  o->offset = new_pos;
  
  oq_push(o, OC_SHOW_DATA);
  MUNLOCK();
  OSIGNAL();
l_thread_error:
  o->orc |= ORC_THREAD_ERROR;
  return;
}

/* input_reader *************************************************************/
static uint8_t C41_CALL input_reader (void * arg)
{
  ochi_t * o = arg;
  acx1_event_t e;
  uint_t c, i;
  uint8_t run_chicken_run;
  int action;

  for (run_chicken_run = 1; run_chicken_run; )
  {
    c = acx1_read_event(&e);
    if (c)
    {
      E(OE_ACX_READ, "failed reading event: $s = $i", acx1_status_str(c), c);
      return ORC_ACX_ERROR;
    }
    switch (e.type)
    {
    case ACX1_RESIZE:
      MLOCK();
      screen_resized(o, e.size.h, e.size.w);
      MUNLOCK();
      OSIGNAL();
      break;
    case ACX1_KEY:
      for (i = 0; i < o->kapv.n && o->kapv.a[i].key != e.km; ++i);
      if (i >= o->kapv.n) break; // unrecognised key
      action = o->kapv.a[i].action;
      switch (action)
      {
      case OA_EXIT:
        run_chicken_run = 0;
        break;
      case OA_CURSOR_LEFT:
      case OA_CURSOR_RIGHT:
      case OA_CURSOR_UP:
      case OA_CURSOR_DOWN:
      case OA_CURSOR_HIGH:
      case OA_CURSOR_MED:
      case OA_CURSOR_LOW:
      case OA_CURSOR_GO:
      case OA_CURSOR_END:
      case OA_CURSOR_PAGE_UP:
      case OA_CURSOR_PAGE_DOWN:
      case OA_CURSOR_BLOCK_UP:
      case OA_CURSOR_BLOCK_DOWN:
      case OA_CURSOR_BOL:
      case OA_CURSOR_EOL:
      case OA_CURSOR_PERC:
      case OA_DEC_WIDTH:
      case OA_INC_WIDTH:
      case OA_SLIDE_LEFT:
      case OA_SLIDE_RIGHT:
      case OA_SLIDE_UP:
      case OA_SLIDE_DOWN:
        move_cursor(o, action);
        break;
      default:
        break;
      }
      break;
    case ACX1_ERROR:
      E(OE_ACX_READ, "read event returned error!");
      return ORC_ACX_ERROR;
    default:
      run_chicken_run = 0;
    }
  }

  return 0;

l_thread_error:
  return ORC_THREAD_ERROR;
}

/* cached_byte **************************************************************/
static int C41_CALL cached_byte (ochi_t * o, int64_t ofs)
{
  if (ofs < 0 || ofs >= o->io_p->size) return OV_NODATA;
  if (ofs < o->ofs_cache[o->adcx] ||
      ofs >= o->ofs_cache[o->adcx] + o->data_cache[o->adcx].n)
    return OV_UNKNOWN;
  return o->data_cache[o->adcx].a[ofs - o->ofs_cache[o->adcx]];
}

/* render_cached_data *******************************************************/
static uint8_t C41_CALL render_cached_data (ochi_t * o)
{
  uint_t c, i, j, k, sty;
  int v, gunk;

  o->out_data.n = 0;
  for (gunk = i = 0; i < o->data_rows; ++i)
  {
    O("\a$c", (i == o->item_row) ? OS_DOFS : OS_NOFS);
    O(o->ofs_fmt, o->page_offset + i * o->line_items);
    O("\a$c$s", OS_OFS_SEP, o->ofs_sep);

    for (j = 0; j < o->line_items; ++j)
    {
      if (j)
      {
        // put separator between hex
        for (k = 3; k > 0 && (((1 << k) - 1) & j); --k);
        O("\a$c$s", (i == o->item_row) ? OS_DHEX_SEP : OS_HEX_SEP, 
          o->hex_sep[k]);
      }

      v = cached_byte(o, o->page_offset + i * o->line_items + j);

      sty = OS_NHEX;
      if (i == o->item_row || j == o->item_col) sty = OS_DHEX;
      if (i == o->item_row && j == o->item_col) sty = OS_CHEX;

      if (v < 0)
      {
        if (v == OV_UNKNOWN)
        {
          gunk = 1;
          sty += OS_NHEX_UNK - OS_NHEX;
          v = '?';
        }
        else
        {
          sty += OS_NHEX_NODATA - OS_NHEX;
          v = '-';
        }
        O("\a$c$c$c", sty, v, v);
      }
      else
      {
        O("\a$c$U.2Hb", sty, v);
      }
    }
    O("\a$c$s", OS_ABR_SEP, o->abr_sep);
    for (j = 0; j < o->line_items; ++j)
    {
      int sty;
      v = cached_byte(o, o->page_offset + i * o->line_items + j);
      sty = abr_style(v);
      if (i == o->item_row && j == o->item_col) sty += OS_CABR_UNK - OS_ABR_UNK;
      O("\a$c$c", sty, abr_ch(v));
    }

    O("\a$c $c", OS_NORMAL, 0);
  }
  if (gunk) 
  {
// printf("REQUEST_READ_DATA\n");
    wq_push(o, OC_READ_DATA);
    JSIGNAL();
  }

  return 0;
l_thread_error:
  return ORC_THREAD_ERROR;
}

/* abr_ch *******************************************************************/
static int abr_ch (int ch)
{
  if (ch < 0) return ch == OV_UNKNOWN ? '?' : ' ';
  if (ch == 0) return '.';
  if (ch < 0x20) return '@' + ch;
  if (ch < 0x7F) return ch;
  if (ch >= 0x80) ch = 0x40 + (ch & 0x3F);
  if (ch == 0x7F) return '~';
  return ch;
}

/* abr_style ****************************************************************/
static int abr_style (int ch)
{
  if (ch < 0) return ch == OV_UNKNOWN ? OS_ABR_UNK : OS_ABR_NODATA;
  if (ch < 0x20 || ch == 0x7F) return OS_ABR_CONTROL;
  if (ch >= '0' && ch <= '9') return OS_ABR_DIGIT;
  if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')) 
    return OS_ABR_LETTER;
  if (ch >= 0x80) return ch < 0xC0 ? OS_ABR_80_BF : OS_ABR_C0_FF;
  return OS_ABR_SYMBOL;
}


/* redraw_screen ************************************************************/
static uint8_t C41_CALL redraw_screen (ochi_t * o) // called in unlocked state
{
  uint8_t lob[0x100];
  ssize_t z;
  z = c41_sfmt(lob, sizeof(lob), "$Dwx$Dw", o->w, o->h);
  A(acx1_set_cursor_mode(0));
  A(acx1_write_start());
  A(acx1_attr(ACX1_BLACK, ACX1_GRAY, 0));
  A(acx1_clear());
  A(acx1_attr(ACX1_DARK_GREEN, ACX1_LIGHT_YELLOW, 0));
  A(acx1_write_pos(o->h, o->w - (int16_t) z));
  A(acx1_write(lob, z));
  A(acx1_write_stop());
  return 0;

l_acx_error:
  return ORC_ACX_ERROR;
}

/* screen_too_small *********************************************************/
static uint8_t C41_CALL screen_too_small (ochi_t * o)
{
  static char const m[] = "SCREEN TOO SMALL";
  A(acx1_set_cursor_mode(0));
  A(acx1_write_start());
  A(acx1_attr(ACX1_DARK_RED, ACX1_LIGHT_YELLOW, ACX1_BOLD));
  A(acx1_clear());
  A(acx1_write_pos(0, 0));
  A(acx1_write(m, sizeof(m) - 1));
  A(acx1_write_stop());
  return 0;

l_acx_error:
  return ORC_ACX_ERROR;
}

/* data_line_len ************************************************************/
static uint_t data_line_len (ochi_t * o, uint_t n)
{
  uint8_t buf[0x20];
  uint_t l, j, k;

  l = c41_sfmt(buf, sizeof(buf), o->ofs_fmt, (uint64_t) 0);
  l += C41_STR_LEN(o->ofs_sep);
  for (j = 0; j < n; ++j)
  {
    if (j)
    {
      // put separator between hex
      for (k = 3; k > 0 && (((1 << k) - 1) & j); --k);
      l += C41_STR_LEN(o->hex_sep[k]);
    }
    l += 2;
  }
  l += C41_STR_LEN(o->abr_sep) + n;
  return (l);
}

/* screen_resized ***********************************************************
 * this is called in the input thread when a resize event is received
 * it should compute all the vars for a correct screen redraw and post the
 * relevant output commands
 * the function should be called with the main mutex locked already
 */
static void C41_CALL screen_resized (ochi_t * o, uint16_t h, uint16_t w)
{
  size_t l;
  uint_t c;

  o->h = h;
  o->w = w;
  o->screen_too_small = (o->h < MIN_HEIGHT || o->w < MIN_WIDTH);
  if (o->screen_too_small) oq_push(o, OC_SHOW_TOO_SMALL);
  else
  {
    if (o->msg_rows > h - 3) o->msg_rows = h - 3;
    o->data_top = 2;
    o->data_rows = h - 1 - o->msg_rows;

    if (data_line_len(o, o->line_items) > w)
    {
      uint_t li;
      for (li = 1; data_line_len(o, 2 << li) <= w; ++li);
      o->line_items = 1 << li;
    }

    oq_push(o, OC_SHOW_TITLE_BAR);
    oq_push(o, OC_SHOW_MSG_AREA);
    oq_push(o, OC_SHOW_DATA);
  }
}

/* render_title *************************************************************/
static uint8_t C41_CALL render_title (ochi_t * o)
{
  uint_t c;

  o->out_data.n = 0;
  O("\a$c", OS_TB_OFS);
  O(o->ofs_fmt, o->offset);
  O("\a$c/\a$c", OS_TB_TEXT, OS_TB_OFS);
  O(o->ofs_fmt, o->io_p->size);
  O("\a$c $s", OS_TB_TEXT, o->fname);
  O("\a$c$c", OS_TB_TEXT, 0);
#if 0
  c = c41_u8v_afmt(&o->out_data, 
                   "\a$c[ochi]\a$c $s $+XG4q/$XG4q (console size: $Dwx$Dw)$c",
                   OS_TB_EM, OS_TB_TEXT, o->fname, o->offset, o->io_p->size,
                   o->w, o->h, 0);
  if (c)
  {
    E(OE_RENDER_TITLE, "failed rendering title (code $i)", c);
    return 1;
  }
#endif

  return 0;
}

/* output_writer ************************************************************/
static uint8_t C41_CALL output_writer (void * arg)
{
  ochi_t * o = arg;
  uint_t c, cmd;
  uint8_t rc, spit;

  MLOCK();
  for (;;)
  {
    for (; !o->exiting && o->oq_bx != o->oq_ex; )
    {
      cmd = o->oq[o->oq_bx];
// printf("output got cmd %u\n", cmd);
      o->oq_bx = (o->oq_bx + 1) & OQ_MASK;

      spit = 0;
      switch (cmd)
      {
      case OC_SHOW_TOO_SMALL:
        MUNLOCK();
        rc = screen_too_small(o);
        if (rc) return rc;
        MLOCK();
        break;
      case OC_SHOW_TITLE_BAR:
        if (render_title(o)) goto l_render_error;
        o->out_row = 1;
        o->out_col = 1;
        o->out_height = 1;
        o->out_width = o->w;
        spit = 1;
        break;
      case OC_SHOW_DATA:
// printf("rendering cached data...\n");
        rc = render_cached_data(o);
// printf("rendered cached data: %d\n", rc);
        if (rc) { MUNLOCK(); return rc; }
        o->out_row = o->data_top;
        o->out_col = 1;
        o->out_height = o->data_rows;
        o->out_width = o->w;
        spit = 1;
        break;
      case OC_SHOW_MSG_AREA:
        break;
      }
      if (spit)
      {
        void * * pp;
        uint8_t * p;
        uint8_t * q;
        MUNLOCK();
        o->out_lines.n = 0;
        for (p = o->out_data.a, q = p + o->out_data.n; p != q; )
        {
          pp = c41_pv_append(&o->out_lines, 1);
          if (!pp)
          {
            E(OE_MEM_ERROR, "failed appending to line table");
            return ORC_MEM_ERROR;
          }
          *pp = p;
          p += C41_STR_LEN(p) + 1;
        }
        if (o->out_lines.n != o->out_height)
        {
          E(OE_RENDER_DATA, "ol=$z, oh=$z", o->out_lines.n, o->out_height);
          return ORC_MISC_ERROR;
        }


        A(acx1_write_start());
        A(acx1_rect((uint8_t const * const *) o->out_lines.a,
                    o->out_row, o->out_col, o->out_height, o->out_width,
                    o->attr_a));
        A(acx1_write_stop());
        MLOCK();
      }
    }
    if (o->exiting) break;
    OWAIT();
  }
  MUNLOCK();

  return 0;

l_render_error:
  MUNLOCK();
  return ORC_MISC_ERROR;
l_acx_error:
  return ORC_ACX_ERROR;
l_thread_error:
  return ORC_THREAD_ERROR;
// l_mem_error:
//   return ORC_MEM_ERROR;
}

/* extend_cache *************************************************************/
static size_t extend_cache (ochi_t * o, int index, size_t len)
{
  uint_t c;
  if (o->data_cache[index].n < len)
  {
    c = c41_u8v_extend(&o->data_cache[index], len - o->data_cache[index].n);
    if (c)
    {
      E(ORC_MEM_ERROR, "failed extending data cache to $z", len);
      len = o->data_cache[index].m;
    }
  }
  //o->data_cache[index].n = len;
  return len;
}

/* worker *******************************************************************/
static uint8_t C41_CALL worker (void * arg)
{
  ochi_t * o = arg;
  uint_t cmd, ci, c;
  ssize_t l, rl;
  int sig;
  int64_t ofs;

  MLOCK();
  for (;;)
  {
    for (; !o->exiting && o->wq_bx != o->wq_ex;)
    {
      cmd = o->wq[o->wq_bx];
//printf("Worker: got cmd=%u\n", cmd);
      o->wq_bx = (o->wq_bx + 1) & WQ_MASK;
      switch (cmd)
      {
      case OC_READ_DATA:
        ci = o->adcx ^ 1;
        l = o->line_items * o->data_rows;
        ofs = o->page_offset;
        if (ofs + l < 0 || ofs >= o->io_p->size) break;
        if (ofs < 0)
        {
          l += ofs;
          ofs = 0;
        }
        if (ofs + l > o->io_p->size)
        {
          l = o->io_p->size - ofs;
        }
        if (!l) break;

        o->ofs_cache[ci] = ofs;
//printf("READ_DATA %u\n", (int) l);
        MUNLOCK();
        l = extend_cache(o, ci, l);
//printf("cache extended to %u\n", (int) l);
        c = c41_io_p64read(o->io_p, o->data_cache[ci].a, o->ofs_cache[ci], l, 
                           &rl);
        if (c)
        {
          E(ORC_FILE_ERROR, "failed reading ofs=$Xq, size=$Xz, status=$u=$s",
            o->ofs_cache[ci], l, c, c41_io_status_name(c));
          rl = 0;
        }
// printf("read from %"PRIi64": %lu\n", o->ofs_cache[ci], (long) rl);
        o->data_cache[ci].n = rl;
        MLOCK();
        o->adcx = ci;
        if (!c && rl) 
        {
          oq_push(o, OC_SHOW_DATA);
          OSIGNAL();
        }
        break;
      }
    }
    if (o->exiting) break;
    JWAIT();
  }
  sig = --(o->workers_left);
  MUNLOCK();
  if (sig) JSIGNAL();
  return 0;

l_thread_error:
  return ORC_THREAD_ERROR;
}

/* set_cmd ******************************************************************/
static int set_cmd (ochi_t * o, uint_t cmd)
{
  if (o->cmd)
  {
    E(OE_TWO_CMD, "too many commands ($i, $i)", o->cmd, cmd);
    o->cmd = OCMD_NOP;
    return 1;
  }
  o->cmd = cmd;
  return 0;
}

/* init_default_styles ******************************************************/
static void init_default_styles (ochi_t * o)
{
#define S(_s, _bg, _fg, _mode) (\
  o->attr_a[_s].bg = (_bg), \
  o->attr_a[_s].fg = (_fg), \
  o->attr_a[OS_NORMAL].mode = (_mode))
  S(OS_NORMAL, ACX1_BLACK, ACX1_LIGHT_GRAY, ACX1_NORMAL);
  S(OS_OFS_SEP, ACX1_BLACK, ACX1_DARK_GRAY, ACX1_NORMAL);
  S(OS_HEX_SEP, ACX1_BLACK, ACX1_DARK_GRAY, ACX1_NORMAL);
  S(OS_DHEX_SEP, ACX1_DARK_BLUE, ACX1_LIGHT_GRAY, ACX1_NORMAL);
  S(OS_ABR_SEP, ACX1_BLACK, ACX1_LIGHT_GRAY, ACX1_NORMAL);
  S(OS_NOFS, ACX1_BLACK, ACX1_LIGHT_BLUE, ACX1_NORMAL);
  S(OS_DOFS, ACX1_DARK_CYAN, ACX1_LIGHT_RED, ACX1_NORMAL);
  S(OS_NHEX, ACX1_BLACK, ACX1_LIGHT_GRAY, ACX1_NORMAL);
  S(OS_SHEX, ACX1_BLACK, ACX1_LIGHT_GRAY, ACX1_NORMAL);
  S(OS_HHEX, ACX1_BLACK, ACX1_LIGHT_GRAY, ACX1_NORMAL);
  S(OS_DHEX, ACX1_DARK_BLUE, ACX1_LIGHT_GRAY, ACX1_NORMAL);
  S(OS_CHEX, ACX1_LIGHT_YELLOW, ACX1_BLACK, ACX1_NORMAL);
  S(OS_NHEX_NODATA, ACX1_BLACK, ACX1_DARK_GRAY, ACX1_NORMAL);
  S(OS_SHEX_NODATA, ACX1_BLACK, ACX1_DARK_GRAY, ACX1_NORMAL);
  S(OS_HHEX_NODATA, ACX1_BLACK, ACX1_DARK_GRAY, ACX1_NORMAL);
  S(OS_DHEX_NODATA, ACX1_DARK_BLUE, ACX1_LIGHT_GRAY, ACX1_NORMAL);
  S(OS_CHEX_NODATA, ACX1_DARK_YELLOW, ACX1_DARK_GRAY, ACX1_NORMAL);
  S(OS_NHEX_UNK, ACX1_BLACK, ACX1_DARK_GRAY, ACX1_NORMAL);
  S(OS_SHEX_UNK, ACX1_BLACK, ACX1_DARK_GRAY, ACX1_NORMAL);
  S(OS_HHEX_UNK, ACX1_BLACK, ACX1_DARK_GRAY, ACX1_NORMAL);
  S(OS_DHEX_UNK, ACX1_DARK_BLUE, ACX1_LIGHT_GRAY, ACX1_NORMAL);
  S(OS_CHEX_UNK, ACX1_DARK_YELLOW, ACX1_DARK_GRAY, ACX1_NORMAL);
  S(OS_ABR_UNK, ACX1_BLACK, ACX1_DARK_GRAY, ACX1_NORMAL);
  S(OS_ABR_NODATA, ACX1_BLACK, ACX1_LIGHT_BLUE, ACX1_NORMAL);
  S(OS_ABR_CONTROL, ACX1_BLACK, ACX1_LIGHT_RED, ACX1_NORMAL);
  S(OS_ABR_SYMBOL, ACX1_BLACK, ACX1_LIGHT_YELLOW, ACX1_NORMAL);
  S(OS_ABR_DIGIT, ACX1_BLACK, ACX1_LIGHT_GREEN, ACX1_NORMAL);
  S(OS_ABR_LETTER, ACX1_BLACK, ACX1_LIGHT_MAGENTA, ACX1_NORMAL);
  S(OS_ABR_80_BF, ACX1_BLACK, ACX1_LIGHT_BLUE, ACX1_NORMAL);
  S(OS_ABR_C0_FF, ACX1_BLACK, ACX1_LIGHT_CYAN, ACX1_NORMAL);
  S(OS_CABR_UNK, ACX1_DARK_YELLOW, ACX1_LIGHT_BLUE, ACX1_NORMAL);
  S(OS_CABR_NODATA, ACX1_DARK_YELLOW, ACX1_LIGHT_BLUE, ACX1_NORMAL);
  S(OS_CABR_CONTROL, ACX1_DARK_YELLOW, ACX1_LIGHT_RED, ACX1_NORMAL);
  S(OS_CABR_SYMBOL, ACX1_DARK_YELLOW, ACX1_LIGHT_YELLOW, ACX1_NORMAL);
  S(OS_CABR_DIGIT, ACX1_DARK_YELLOW, ACX1_LIGHT_GREEN, ACX1_NORMAL);
  S(OS_CABR_LETTER, ACX1_DARK_YELLOW, ACX1_LIGHT_MAGENTA, ACX1_NORMAL);
  S(OS_CABR_80_BF, ACX1_DARK_YELLOW, ACX1_LIGHT_BLUE, ACX1_NORMAL);
  S(OS_CABR_C0_FF, ACX1_DARK_YELLOW, ACX1_LIGHT_CYAN, ACX1_NORMAL);
  S(OS_TB_TEXT, ACX1_BLACK, ACX1_LIGHT_GRAY, ACX1_NORMAL);
  S(OS_TB_NAME, ACX1_BLACK, ACX1_LIGHT_GRAY, ACX1_NORMAL);
  S(OS_TB_OFS, ACX1_BLACK, ACX1_LIGHT_GRAY, ACX1_NORMAL);
  S(OS_TB_EM, ACX1_BLACK, ACX1_WHITE, ACX1_NORMAL);
#undef S
}

/* add_key_action ***********************************************************/
static uint_t add_key_action (ochi_t * o, uint32_t key, uint32_t action)
{
  ochi_kap_t * p;

  p = kapv_append(&o->kapv, 1);
  if (!p) 
  {
    E(OE_MEM_ERROR, "failed to append to key-action vector");
    o->orc = ORC_MEM_ERROR;
    return 1;
  }
  p->key = key;
  p->action = action;
  return 0;
}

/* init_default_keys ********************************************************/
static uint_t init_default_keys (ochi_t * o)
{
  return 0
    || add_key_action(o, ACX1_ALT | 'x'         , OA_EXIT)
    || add_key_action(o, ACX1_ESC               , OA_EXIT)
    || add_key_action(o, 'q'                    , OA_EXIT)
    || add_key_action(o, 'h'                    , OA_CURSOR_LEFT)
    || add_key_action(o, ACX1_LEFT              , OA_CURSOR_LEFT)
    || add_key_action(o, 'j'                    , OA_CURSOR_DOWN)
    || add_key_action(o, ACX1_DOWN              , OA_CURSOR_DOWN)
    || add_key_action(o, 'k'                    , OA_CURSOR_UP)
    || add_key_action(o, ACX1_UP                , OA_CURSOR_UP)
    || add_key_action(o, 'l'                    , OA_CURSOR_RIGHT)
    || add_key_action(o, ACX1_RIGHT             , OA_CURSOR_RIGHT)
    || add_key_action(o, 'H'                    , OA_CURSOR_HIGH)
    || add_key_action(o, 'M'                    , OA_CURSOR_MED)
    || add_key_action(o, 'L'                    , OA_CURSOR_LOW)
    || add_key_action(o, 'g'                    , OA_CURSOR_GO)
    || add_key_action(o, ACX1_CTRL | ACX1_HOME  , OA_CURSOR_GO)
    || add_key_action(o, ACX1_CTRL | ACX1_PAGE_UP, OA_CURSOR_GO)
    || add_key_action(o, 'G'                    , OA_CURSOR_END)
    || add_key_action(o, ACX1_CTRL | ACX1_END   , OA_CURSOR_END)
    || add_key_action(o, ACX1_CTRL | ACX1_PAGE_DOWN, OA_CURSOR_END)
    || add_key_action(o, '^'                    , OA_CURSOR_BOL)
    || add_key_action(o, ACX1_HOME              , OA_CURSOR_BOL)
    || add_key_action(o, '$'                    , OA_CURSOR_EOL)
    || add_key_action(o, ACX1_END               , OA_CURSOR_EOL)
    || add_key_action(o, '%'                    , OA_CURSOR_PERC)
    || add_key_action(o, 'B' | ACX1_CTRL        , OA_CURSOR_PAGE_UP)
    || add_key_action(o, ACX1_PAGE_UP           , OA_CURSOR_PAGE_UP)
    || add_key_action(o, 'F' | ACX1_CTRL        , OA_CURSOR_PAGE_DOWN)
    || add_key_action(o, ACX1_PAGE_DOWN         , OA_CURSOR_PAGE_DOWN)
    || add_key_action(o, 'U' | ACX1_CTRL        , OA_CURSOR_BLOCK_UP)
    || add_key_action(o, 'D' | ACX1_CTRL        , OA_CURSOR_BLOCK_DOWN)
    || add_key_action(o, '%'                    , OA_CURSOR_PERC)
    || add_key_action(o, '-'                    , OA_DEC_WIDTH)
    || add_key_action(o, '='                    , OA_INC_WIDTH)
    || add_key_action(o, '<'                    , OA_SLIDE_LEFT)
    || add_key_action(o, ACX1_CTRL | ACX1_LEFT  , OA_SLIDE_LEFT)
    || add_key_action(o, '>'                    , OA_SLIDE_RIGHT)
    || add_key_action(o, ACX1_CTRL | ACX1_RIGHT , OA_SLIDE_RIGHT)
    || add_key_action(o, ACX1_CTRL | 'E'        , OA_SLIDE_DOWN)
    || add_key_action(o, ACX1_CTRL | 'Y'        , OA_SLIDE_UP)
    ;
}

/* init *********************************************************************/
static void init (ochi_t * o, c41_cli_t * cli_p)
{
  uint_t state, i, j;
  uint_t ma_rc, c;

  C41_VAR_ZERO(*o);
  o->ma_p = cli_p->ma_p;
  o->smt_p = cli_p->smt_p;
  o->fsi_p = cli_p->fsi_p;
  o->fspi_p = cli_p->fspi_p;
  c41_u8v_init(&o->emsg, cli_p->ma_p, 20);
  c41_u8v_init(&o->out_data, o->ma_p, 16);
  c41_u8v_init(&o->data_cache[0], o->ma_p, 16);
  c41_u8v_init(&o->data_cache[1], o->ma_p, 16);
  c41_pv_init(&o->out_lines, o->ma_p, 16);
  kapv_init(&o->kapv, o->ma_p, 8);

  o->data_top = 2;
  o->msg_rows = 5;
  o->mode = OM_HEX_ABR;
  o->line_items = 0x40;
  o->ofs_sep = ": ";
  o->abr_sep = "  ";
  o->hex_sep[0] = " ";
  o->hex_sep[1] = " ";
  o->hex_sep[2] = "  ";
  o->hex_sep[3] = "  ";

  init_default_styles(o);
  if (init_default_keys(o)) return;

  if (!cli_p->arg_n)
  {
    o->cmd = OCMD_HELP;
    return;
  }

  c = c41_smt_mutex_create(&o->main_mutex_p, o->smt_p, o->ma_p);
  if (c)
  {
    E(OE_MAIN_MUTEX_CREATE, "failed creating mutex: $i", c);
    return;
  }

  for (state = 0, i = j = 0; i < cli_p->arg_n; )
  {
    switch (state)
    {
    case 0:
      if (cli_p->arg_a[i][0] == '-')
      {
        if (cli_p->arg_a[i][1] == '-')
        {
          // long option
          if (cli_p->arg_a[i][2] == 0) { state = 1; break; }
          if (C41_STR_EQUAL(cli_p->arg_a[i] + 2, "help"))
          {
            if (set_cmd(o, OCMD_HELP)) return;
            ++i;
            break;
          }
          if (C41_STR_EQUAL(cli_p->arg_a[i] + 2, "test"))
          {
            if (set_cmd(o, OCMD_TEST_UI)) return;
            ++i;
            break;
          }

          E(OE_UNKNOWN_OPT, "unknown option '$s'", cli_p->arg_a[i] + 2);
          o->orc |= ORC_CLI_ERROR;
          return;
        }
        else
        {
          // start of short opts
          if (cli_p->arg_a[i][1] == 0)
          {
            E(OE_MISS_SH_OPT, "missing short option at arg $i", i + 1);
            return;
          }
          state = 2;
          j = 1;
          continue;
        }
      }
    case 1:
      if (!o->cmd) o->cmd = OCMD_MAIN;
      if (o->fname)
      {
        E(OE_BAD_CLI, "only one file name allowed");
        o->orc |= ORC_CLI_ERROR;
        return;
      }

      o->fname = cli_p->arg_a[i++];
      break;
    case 2:
      switch (cli_p->arg_a[i][j])
      {
      case 0:
        state = 0;
        ++i;
        break;
      case 'h':
        if (set_cmd(o, OCMD_HELP)) return;
        ++j;
        break;
      case 'l':
        // todo: open log
        break;
      case 't':
        if (set_cmd(o, OCMD_TEST_UI)) return;
       ++j;
        break;
      default:
        E(OE_UNKNOWN_OPT, "unknown short option '$c'", cli_p->arg_a[i][j]);
        o->orc |= ORC_CLI_ERROR;
        return;
      }
    }
  }
}

/* finish *******************************************************************/
static void finish (ochi_t * o)
{
  uint_t c;

  if (o->main_mutex_p)
  {
    c = c41_smt_mutex_destroy(o->main_mutex_p, o->smt_p, o->ma_p);
    if (c) E(OE_MAIN_MUTEX_DESTROY, "failed destroying mutex: $i", c);
  }

  c41_u8v_free(&o->data_cache[0]);
  c41_u8v_free(&o->data_cache[1]);
}

/* run_ui *******************************************************************/
static void run_ui (ochi_t * o)
{
  uint_t c;
  int tc;
  uint16_t h, w;

  c = acx1_init();
  if (c)
  {
    E(OE_ACX_INIT,
      "failed initialising console ($s: $i)", acx1_status_str(c), c);
    o->orc |= ORC_ACX_ERROR;
    return;
  }
  o->acx_inited = 1;

  do
  {
    MLOCK();
    c = acx1_get_screen_size(&h, &w);
    if (c)
    {
      E(OE_ACX_READ, "failed reading screen size");
      o->orc |= ORC_ACX_ERROR;
      break;
    }
    c = acx1_set_cursor_mode(0);
    if (c)
    {
      E(OE_ACX_READ, "failed hiding cursor");
      o->orc |= ORC_ACX_ERROR;
      break;
    }

    c = c41_smt_thread_create(o->smt_p, &o->input_tid, input_reader, o);
    if (c)
    {
      E(OE_INPUT_THREAD_CREATE, "failed creating input thread: $i", c);
      break;
    }
    o->input_thread_inited = 1;

    c = c41_smt_cond_create(&o->out_cond_p, o->smt_p, o->ma_p);
    if (c)
    {
      E(OE_OUTPUT_COND_CREATE, 
        "failed creating output condition variable $i", c);
      break;
    }
    o->out_cond_inited = 1;

    c = c41_smt_thread_create(o->smt_p, &o->output_tid, output_writer, o);
    if (c)
    {
      E(OE_OUTPUT_THREAD_CREATE, "failed creating output thread: $i", c);
      break;
    }
    o->out_thread_inited = 1;

    screen_resized(o, h, w);

    //printf("unlocking (line %d)...\n", __LINE__);
    c = c41_smt_mutex_unlock(o->smt_p, o->main_mutex_p);
    if (c) {
      E(OE_MAIN_MUTEX_UNLOCK, "main mutex unlock error: $i", c);
      o->orc |= ORC_THREAD_ERROR;
    }
    //printf("unlocked (line %d)...\n", __LINE__); 
  }
  while (0);

  if (o->input_thread_inited && !o->orc)
  {
    tc = c41_smt_thread_join(o->smt_p, o->input_tid);
    if (tc < 0)
    {
      E(OE_INPUT_THREAD_JOIN, "failed joining input thread: $i", tc);
      o->orc |= ORC_THREAD_ERROR;
    }
    else o->orc |= tc;
    // c = c41_smt_mutex_lock(o->smt_p, o->main_mutex_p);
    // if (c) {
    //   E(OE_MAIN_MUTEX_LOCK, "main mutex lock error: $i", c);
    //   o->orc |= ORC_THREAD_ERROR;
    // }
  }

  o->exiting = 1;

  if (o->out_cond_inited)
  {
    tc = c41_smt_cond_signal(o->smt_p, o->out_cond_p);
    if (tc)
    {
      E(OE_OUTPUT_COND_SIGNAL, "failed signalling cond var: $i", tc);
      o->orc |= ORC_THREAD_ERROR;
    }
  }

  if (o->out_thread_inited)
  {
    tc = c41_smt_thread_join(o->smt_p, o->output_tid);
    if (tc < 0)
    {
      E(OE_OUTPUT_THREAD_JOIN, "failed joining output thread: $i", tc);
      o->orc |= ORC_THREAD_ERROR;
    }
  }

  if (o->out_cond_inited)
  {
    tc = c41_smt_cond_destroy(o->out_cond_p, o->smt_p, o->ma_p);
    if (tc)
    {
      E(OE_OUTPUT_COND_DESTROY, "failed destroying condition variable: $i", tc);
      o->orc |= ORC_THREAD_ERROR;
    }
  }

  if (o->acx_inited)
  {
    acx1_write_start();
    acx1_attr(ACX1_BLACK, ACX1_GRAY, 0);
    acx1_clear();
    acx1_write_stop();
    acx1_set_cursor_mode(1);
    acx1_finish();
  }
  o->acx_inited = 0;
  return;
l_thread_error:
  o->orc |= ORC_THREAD_ERROR;
}

/* test_ui ******************************************************************/
static void test_ui (ochi_t * o)
{
  run_ui(o);
}

/* start_workers ************************************************************/
static uint8_t start_workers (ochi_t * o)
{
  uint_t i, c;

  c = c41_smt_cond_create(&o->job_cond_p, o->smt_p, o->ma_p);
  if (c)
  {
    E(OE_WORKER_COND_CREATE, 
      "failed creating job condition variable $i", c);
    return ORC_THREAD_ERROR;
  }
  o->job_cond_inited = 1;

  for (i = 0; i < NUM_WORKERS; ++i)
  {
    c = c41_smt_thread_create(o->smt_p, &o->worker_tid_a[i], worker, o);
    if (c)
    {
      E(OE_WORKER_THREAD_CREATE,
        "failed creating worker #$i (error code $i)", i, c);
      return ORC_THREAD_ERROR;
    }
  }
  o->worker_n = i;
  o->workers_left = i;

  return 0;
}

/* stop_workers *************************************************************/
static uint8_t stop_workers (ochi_t * o)
{
  uint_t i, c;
  int tc;

  JSIGNAL();
  for (i = 0; i < o->worker_n; ++i)
  {
    tc = c41_smt_thread_join(o->smt_p, o->worker_tid_a[i]);
    if (tc < 0)
    {
      E(OE_WORKER_THREAD_JOIN, "failed joining worker thread #$i (error $i)",
        i, tc);
      o->orc |= ORC_THREAD_ERROR;
    }
  }

  if (o->job_cond_inited)
  {
    c = c41_smt_cond_destroy(o->job_cond_p, o->smt_p, o->ma_p);
    if (c)
    {
      E(OE_WORKER_COND_DESTROY, 
        "failed destroying job condition var (code $i)", c);
    }
  }

  return 0;
l_thread_error:
  return ORC_THREAD_ERROR;
}

/* cmd_main *****************************************************************/
static void cmd_main (ochi_t * o)
{
  uint_t i, inc, ma_rc, io_rc, fsi_rc;
  c41_u8v_t path;
  ssize_t z;

  c41_u8v_init(&path, o->ma_p, 16);
  path.n = 0;
  z = o->fspi_p->fsp_from_utf8(path.a, path.m, o->fname,
                               C41_STR_LEN(o->fname));
  if (z < 0)
  {
    E(OE_FILE_NAME, "bad file name '$s'", o->fname);
    o->orc |= ORC_FILE_ERROR;
    return;
  }
  ma_rc = c41_u8v_extend(&path, z);
  if (ma_rc)
  {
    E(OE_MEM_ERROR, "failed allocating path for '$s'", o->fname);
    o->orc |= ORC_MEM_ERROR;
    return;
  }
  z = o->fspi_p->fsp_from_utf8(path.a, path.m, o->fname,
                               C41_STR_LEN(o->fname));
  fsi_rc = c41_file_open_fsp(path.a, z,
                    C41_FSI_EXF_OPEN | C41_FSI_NEWF_REJECT | C41_FSI_READ, 
                    o->fsi_p, &o->io_p);
  if (fsi_rc)
  {
    E(OE_FILE_OPEN, "file open failed for '$s': $s = $i",
      o->fname, c41_fsi_status_name(fsi_rc), fsi_rc);
    o->orc |= ORC_FILE_ERROR;
  }
  ma_rc = c41_u8v_free(&path);
  if (ma_rc)
  {
    E(OE_MEM_ERROR, "failed freeing path array");
    o->orc |= ORC_MEM_ERROR;
  }

  if (o->io_p)
  {
    fsi_rc = c41_io_get_size(o->io_p);
    if (fsi_rc)
    {
      E(OE_FILE_SEEK, "file seek error $s = $i",
        c41_fsi_status_name(fsi_rc), fsi_rc);
      o->orc |= ORC_FILE_ERROR;
    }
    else prepare_offset_format(o);
  }


  if (!o->orc)
  {
    o->orc |= start_workers(o);
    run_ui(o);
    stop_workers(o);
  }

  if (o->io_p)
  {
    io_rc = c41_io_close(o->io_p);
    if (io_rc)
    {
      E(OE_FILE_CLOSE, "error closing file '$s'", o->fname);
    }
  }
}

/* prepare_offset_format ****************************************************/
static void prepare_offset_format (ochi_t * o)
{
  uint_t i;
  uint64_t l = o->io_p->size + 0x1000;

  for (i = 4; i < 16 && l >= ((uint64_t) 1 << (i * 4)); ++i);
  c41_sfmt(o->ofs_fmt, sizeof(o->ofs_fmt), "$$+HG4.$iq", i);
  //printf("fmt: %s\n", o->ofs_fmt);
}

/* hmain ********************************************************************/
uint8_t C41_CALL hmain (c41_cli_t * cli_p)
{
  uint_t c;
  ssize_t z;
  ochi_t os;
  ochi_t * o = &os;

  init(o, cli_p);

  switch (o->cmd)
  {
  case OCMD_NOP:
    break;
  case OCMD_HELP:
    help(o, cli_p->stdout_p);
    break;
  case OCMD_TEST_UI:
    test_ui(o);
    break;
  case OCMD_MAIN:
    cmd_main(o);
    break;
  default:
    E(OE_NO_CODE, "command $i not implemented", o->cmd);
    break;
  }

  finish(o);

  if (os.emsg.n)
  {
    z = c41_io_fmt(cli_p->stderr_p,
                   "\n=== ERROR ===\n$.*s===\n", os.emsg.n, os.emsg.a);
    if (z < 0) os.orc |= ORC_REPORT_ERROR;
    c41_u8v_free(&os.emsg);
  }

  return os.orc;
}

/* vim: set sw=2 sts=2: */
