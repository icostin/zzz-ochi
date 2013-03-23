#include <c41.h>
#include <hbs1.h>
#include <acx1.h>

// ochi return code
#define ORC_ACX_ERROR 1
#define ORC_FILE_ERROR 2
#define ORC_MEM_ERROR 4
#define ORC_REPORT_ERROR 8 // error reporting error

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

typedef struct ochi_s                           ochi_t;
struct ochi_s
{
  uint_t        cmd;
  uint_t        fn_n;
  uint8_t const * * fn_a;

  uint8_t       orc; // return code
  uint_t        eid; // error id
  c41_u8v_t     emsg;
  c41_ma_t *    ma_p;
  uint8_t       acx_inited;
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

  return 0;
}

/* output_writer ************************************************************/
uint8_t C41_CALL output_writer (void * arg)
{
  ochi_t * o = arg;

  return 0;
}


/* test_ui ******************************************************************/
static void test_ui (ochi_t * o)
{
  acx1_event_t e;

  acx1_read_event(&e);
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
  uint_t ma_rc;

  C41_VAR_ZERO(*o);
  o->ma_p = cli_p->ma_p;
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

/* hmain ********************************************************************/
uint8_t C41_CALL hmain (c41_cli_t * cli_p)
{
  uint_t c;
  ssize_t z;
  ochi_t os;
  ochi_t * o = &os;

  init(&os, cli_p);

  if (os.cmd >= OCMD_WITH_UI)
  {
    c = acx1_init();
    if (c)
    {
      E(OE_ACX_INIT,
        "failed initialising console ($s: $i)", acx1_status_str(c), c);
      os.cmd = OCMD_NOP;
      os.orc |= ORC_ACX_ERROR;
    }
    else os.acx_inited = 1;
  }

  switch (os.cmd)
  {
  case OCMD_NOP:
    break;
  case OCMD_HELP:
    help(&os, cli_p->stdout_p);
    break;
  case OCMD_TEST_UI:
    test_ui(&os);
    break;
  default:
    E(OE_NO_CODE, "command $i not implemented", os.cmd);
    break;
  }

  if (os.acx_inited)
  {
    acx1_finish();
  }

  if (os.emsg.n)
  {
    z = c41_io_fmt(cli_p->stderr_p,
                   "\n=== ERROR ===\n$.*s===\n", os.emsg.n, os.emsg.a);
    if (z < 0) os.orc |= ORC_REPORT_ERROR;
  }

  return os.orc;
}

