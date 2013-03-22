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
  default:
    return "THE_ERROR_WE_DO_NOT_PRINT_ITS_NAME";
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

  uint8_t       orc; // return code
  uint_t        eid; // error id
  c41_u8v_t     emsg;
  c41_ma_t *    ma_p;
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

/* init *********************************************************************/
static void init (ochi_t * o, c41_cli_t * cli_p)
{
  uint_t state, i, j;

  C41_VAR_ZERO(*o);
  o->ma_p = cli_p->ma_p;
  c41_u8v_init(&o->emsg, cli_p->ma_p, 20);

  if (!cli_p->arg_n)
  {
    o->cmd = OCMD_HELP;
    return;
  }

#if 0
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
        }
        else
        {
          // start of short opts
        }

      }
    }
  }
#endif
  E(OE_NO_CODE, "init not implemented");
}


/* hmain ********************************************************************/
uint8_t C41_CALL hmain (c41_cli_t * cli_p)
{
  uint_t c;
  ssize_t z;
  ochi_t os;
  ochi_t * o = &os;

  init(&os, cli_p);

  switch (os.cmd)
  {
  case OCMD_NOP:
    break;
  case OCMD_HELP:
    help(&os, cli_p->stdout_p);
    break;
  default:
    E(OE_NO_CODE, "command $i not implemented", os.cmd);
    break;
  }


  if (os.emsg.n)
  {
    z = c41_io_fmt(cli_p->stderr_p, 
                   "\n=== ERROR ===\n$.*s===\n", os.emsg.n, os.emsg.a);
    if (z < 0) os.orc |= ORC_REPORT_ERROR;
  }


  return os.orc;
}

