#include <c41.h>
#include <hbs1.h>
#include <acx1.h>

#define MIN_WIDTH               40
#define MIN_HEIGHT              10

// ochi return code
#define ORC_ACX_ERROR           0x01
#define ORC_FILE_ERROR          0x02
#define ORC_MEM_ERROR           0x04
#define ORC_REPORT_ERROR        0x08 // error reporting error
#define ORC_THREAD_ERROR        0x10
#define ORC_CLI_ERROR           0x20

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
  OE_FILE_CLOSE,
  OE_FILE_OPEN,
  OE_FILE_NAME,
};
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
  default:
    return "THE_ERROR_WHOSE_NAME_WE_DO_NOT_PRINT";
  }
}

enum ochi_cmd_enum
{
  OCMD_NOP = 0,
  OCMD_ARG_ERROR,
  OCMD_HELP,
  OCMD_WITH_UI,
  OCMD_MAIN = OCMD_WITH_UI,
  OCMD_TEST_UI,
};

#define MLOCK() do { uint_t c;                                  \
    c = c41_smt_mutex_lock(o->smt_p, o->main_mutex_p);          \
    if (c) {                                                    \
      E(OE_MAIN_MUTEX_LOCK, "main mutex lock error: $i", c);    \
      o->orc |= ORC_THREAD_ERROR;                               \
      goto l_thread_error;                                      \
    }                                                           \
  } while (0)

#define MUNLOCK() do { uint_t c;                                \
    c = c41_smt_mutex_unlock(o->smt_p, o->main_mutex_p);        \
    if (c) {                                                    \
      E(OE_MAIN_MUTEX_UNLOCK, "main mutex unlock error: $i", c);\
      o->orc |= ORC_THREAD_ERROR;                               \
      goto l_thread_error;                                      \
    }                                                           \
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

#define A(_x) do { uint_t c = (_x); \
    if (c) { E(OE_ACX_WRITE, "($s) failed: $s = $i", #_x, \
               acx1_status_str(c), c); \
      o->orc |= ORC_ACX_ERROR; \
      goto l_acx_error; \
    } \
  } while (0)


#define OQ_SIZE (1 << 4)
#define OQ_MASK (OQ_SIZE - 1)

enum oqcmd_enum
{
  OQCMD_INIT = 0,
  OQCMD_RESIZE,
};

typedef struct ochi_s                           ochi_t;

struct ochi_s
{
  uint_t        cmd;
  uint8_t const * fname;
  c41_io_t * io_p;

  c41_smt_mutex_t * main_mutex_p;
  c41_smt_cond_t * out_cond_p;

  uint8_t       oq[OQ_SIZE];
  uint8_t       oq_bx, oq_ex;

  c41_smt_tid_t input_tid;
  c41_smt_tid_t output_tid;

  uint16_t      hn, wn;
  uint16_t      h, w;

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
  uint8_t       exiting;
  uint8_t       screen_too_small;
};

#define E(_eid, ...)                                                          \
  if (!(c41_u8v_afmt(&o->emsg, __VA_ARGS__) ||                                \
        c41_u8v_afmt(&o->emsg, " (code $i: $s, line $i)\n",                   \
                     (o->eid = (_eid)), ename(_eid), __LINE__))) ;            \
  else (o->orc |= ORC_REPORT_ERROR)

static char const help_msg[] =
 "ochi - data viewer\n"
 ;

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

/* input_reader *************************************************************/
uint8_t C41_CALL input_reader (void * arg)
{
  ochi_t * o = arg;
  acx1_event_t e;
  uint_t c;
  uint8_t run_chicken_run;

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
      o->hn = e.size.h;
      o->wn = e.size.w;
      oq_push(o, OQCMD_RESIZE);
      MUNLOCK();
      OSIGNAL();
      break;
    case ACX1_KEY:
      if (e.km == (ACX1_ALT | 'x')) run_chicken_run = 0;
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

/* init_screen **************************************************************/
uint8_t C41_CALL init_screen (ochi_t * o) // called in unlocked state
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
uint8_t C41_CALL screen_too_small (ochi_t * o)
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

/* output_writer ************************************************************/
uint8_t C41_CALL output_writer (void * arg)
{
  ochi_t * o = arg;
  uint_t c, cmd;
  uint8_t rc;

  MLOCK();
  for (;;)
  {
    for (; !o->exiting && o->oq_bx != o->oq_ex; )
    {
      cmd = o->oq[o->oq_bx];
      o->oq_bx = (o->oq_bx + 1) & OQ_MASK;

      switch (o->oq[o->oq_bx])
      {
      case OQCMD_INIT:
      case OQCMD_RESIZE:
        o->h = o->hn;
        o->w = o->wn;
        o->screen_too_small = (o->h < MIN_HEIGHT || o->w < MIN_WIDTH);
        MUNLOCK();
        rc = o->screen_too_small ? screen_too_small(o) : init_screen(o);
        if (rc) return rc;
        MLOCK();
        break;
      }
    }
    if (o->exiting) break;
    OWAIT();
  }

  MUNLOCK();

  return 0;

l_acx_error:
  return ORC_ACX_ERROR;
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

  for (state = 0, i = 0; i < cli_p->arg_n; )
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
}

/* run_ui *******************************************************************/
static void run_ui (ochi_t * o)
{
  uint_t c;
  int tc;

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
    c = acx1_get_screen_size(&o->hn, &o->wn);
    if (c)
    {
      E(OE_ACX_READ, "failed reading screen size");
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
      E(OE_OUTPUT_COND_CREATE, "failed creating output condition variable $i",
        c);
      break;
    }
    o->out_cond_inited = 1;

    o->oq[0] = OQCMD_INIT;
    o->oq_ex = 1;

    c = c41_smt_thread_create(o->smt_p, &o->output_tid, output_writer, o);
    if (c)
    {
      E(OE_OUTPUT_THREAD_CREATE, "failed creating output thread: $i", c);
      break;
    }
    o->out_thread_inited = 1;

  }
  while (0);

  if (o->input_thread_inited)
  {
    tc = c41_smt_thread_join(o->smt_p, o->input_tid);
    if (tc < 0)
    {
      E(OE_INPUT_THREAD_JOIN, "failed joining input thread: $i", tc);
      o->orc |= ORC_THREAD_ERROR;
    }
    else o->orc |= tc;
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
}

/* test_ui ******************************************************************/
static void test_ui (ochi_t * o)
{
  run_ui(o);
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
  fsi_rc = c41_file_open(o->fsi_p, &o->io_p, path.a, z, 
                    C41_FSI_EXF_OPEN | C41_FSI_NEWF_REJECT | C41_FSI_READ);
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

  if (!o->orc) run_ui(o);

  if (o->io_p)
  {
    io_rc = c41_io_close(o->io_p);
    if (io_rc)
    {
      E(OE_FILE_CLOSE, "error closing file '$s'", o->fname);
    }
  }
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

