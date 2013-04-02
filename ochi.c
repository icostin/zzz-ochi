#include <c41.h>
#include <hbs1.h>
#include <acx1.h>

// ochi return code
#define ORC_ACX_ERROR           0x01
#define ORC_FILE_ERROR          0x02
#define ORC_MEM_ERROR           0x04
#define ORC_REPORT_ERROR        0x08 // error reporting error
#define ORC_THREAD_ERROR        0x10

enum ochi_err_enum
{
  OE_NONE = 0,
  OE_NO_CODE,
  OE_STDOUT_ERROR,
  OE_TWO_CMD,
  OE_MISS_SH_OPT,
  OE_UNKNOWN_OPT,
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
  OCMD_VIEW_FILE = OCMD_WITH_UI,
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

#define MWAIT() do { uint_t c;                                  \
    c = c41_smt_cond_wait(o->smt_p, o->out_cond_p, o->main_mutex_p); \
    if (c) {                                                    \
      E(OE_OUTPUT_COND_WAIT, "output cond wait error: $i", c);  \
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

#define OQCMD_INIT 0

typedef struct ochi_s                           ochi_t;
struct ochi_s
{
  uint_t        cmd;
  uint_t        fn_n;
  uint8_t const * * fn_a;

  c41_smt_mutex_t * main_mutex_p;
  c41_smt_cond_t * out_cond_p;

  uint8_t       oq[OQ_SIZE];
  uint8_t       oq_bx, oq_ex;

  c41_smt_tid_t input_tid;
  c41_smt_tid_t output_tid;

  uint8_t       orc; // return code
  uint_t        eid; // error id
  c41_u8v_t     emsg;
  c41_ma_t *    ma_p;
  c41_smt_t *   smt_p;
  uint8_t       acx_inited;
  uint8_t       input_thread_inited;
  uint8_t       out_thread_inited;
  uint8_t       out_cond_inited;
  uint8_t       exiting;
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

/* input_reader *************************************************************/
uint8_t C41_CALL input_reader (void * arg)
{
  ochi_t * o = arg;
  acx1_event_t e;
  uint_t c;

  for (;;)
  {
    c = acx1_read_event(&e);
    if (c)
    {
      E(OE_ACX_READ, "failed reading event: $s = $i", acx1_status_str(c), c);
      return ORC_ACX_ERROR;
    }
    if (e.km == (ACX1_ALT | 'x')) break;
  }

  return 0;
}

/* output_writer ************************************************************/
uint8_t C41_CALL output_writer (void * arg)
{
  ochi_t * o = arg;
  uint_t c, cmd;

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
        MUNLOCK();
        A(acx1_write_start());
        A(acx1_clear());
        A(acx1_write_stop());
        MLOCK();
        break;
      }
    }
    if (o->exiting) break;
    MWAIT();
  }

  MUNLOCK();

  return 0;

l_acx_error:
  o->orc |= ORC_ACX_ERROR;
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
  c41_u8v_init(&o->emsg, cli_p->ma_p, 20);

  if (!cli_p->arg_n)
  {
    o->cmd = OCMD_HELP;
    return;
  }

  ma_rc = C41_VAR_ALLOC(o->ma_p, o->fn_a, cli_p->arg_n);
  if (ma_rc)
  {
    E(OE_MEM_ERROR, "alloc error ($s: $i)", c41_ma_status_name(ma_rc), ma_rc);
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
      if (!o->cmd) o->cmd = OCMD_VIEW_FILE;
      o->fn_a[o->fn_n++] = cli_p->arg_a[i++];
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
        return;
      }
    }
  }

  ma_rc = C41_VAR_REALLOC(o->ma_p, o->fn_a, o->fn_n, cli_p->arg_n);
  if (ma_rc)
  {
    E(OE_MEM_ERROR, "alloc error ($s: $i)", c41_ma_status_name(ma_rc), ma_rc);
    return;
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

  if (o->fn_n)
  {
    c = C41_VAR_FREE(o->ma_p, o->fn_a, o->fn_n);
    if (c) E(OE_MEM_ERROR, "failed freeing file names: $s = $i",
             c41_ma_status_name(c), c);
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

  if (o->acx_inited) acx1_finish();
  o->acx_inited = 0;
}

/* test_ui ******************************************************************/
static void test_ui (ochi_t * o)
{
  run_ui(o);
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

