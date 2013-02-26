#include <c41.h>
#include <hbs1.h>
#include <acx1.h>
// #include <stdio.h>
// static FILE * log_file;

#define HMAX 0x100
#define WMAX 0x200
#define L (WMAX * 4)

#define CMD_EXIT 1

//#define acx1_attr(_a, _b, _c) (0)
//#define acx1_write(_a, _b) (0)

enum style_enum
{
  STYLE_NORMAL = 0,
  STYLE_SEPARATOR,
  STYLE_OFFSET,
  STYLE_HEX,
  STYLE_CHAR_SYMBOL,
  STYLE_CHAR_CONTROL,
  STYLE_CHAR_LETTER,
  STYLE_CHAR_DIGIT,
  STYLE_CHAR_80_BF,
  STYLE_CHAR_C0_FF,
  STYLE_NODATA,
  STYLE_ERROR,
  STYLE__COUNT,
};

typedef struct style_data_s style_data_t;
struct style_data_s
{
  uint8_t style, bg, fg, mode;
};

static style_data_t style[] =
{
  { STYLE_NORMAL,               ACX1_BLUE,      ACX1_WHITE,                     0 },
  { STYLE_SEPARATOR,            ACX1_BLACK,     ACX1_DARK_GRAY,                 0 },
  { STYLE_OFFSET,               ACX1_BLACK,     ACX1_LIGHT_BLUE,                0 },
  { STYLE_HEX,                  ACX1_BLACK,     ACX1_GRAY,                      0 },
  { STYLE_CHAR_SYMBOL,          ACX1_BLACK,     ACX1_LIGHT_YELLOW,              0 },
  { STYLE_CHAR_CONTROL,         ACX1_BLACK,     ACX1_DARK_RED,                  0 },
  { STYLE_CHAR_LETTER,          ACX1_BLACK,     ACX1_LIGHT_MAGENTA,             0 },
  { STYLE_CHAR_DIGIT,           ACX1_BLACK,     ACX1_LIGHT_GREEN,               0 },
  { STYLE_CHAR_80_BF,           ACX1_BLACK,     ACX1_DARK_GREEN,                0 },
  { STYLE_CHAR_C0_FF,           ACX1_BLACK,     ACX1_DARK_CYAN,                 0 },
  { STYLE_NODATA,               ACX1_BLACK,     ACX1_LIGHT_BLUE,                0 },
  { STYLE_ERROR,                ACX1_DARK_RED,  ACX1_LIGHT_YELLOW,              0 },
};

acx1_attr_t attrs[STYLE__COUNT];

char err_msg[0x100] = { 0 };

#define A(_expr) if (!(_expr)) ; else { c41_sfmt(err_msg, sizeof(err_msg), "$s:$i: ($s) failed", __FILE__, __LINE__, #_expr); goto l_aerr; }

/* view *********************************************************************/
uint_t C41_CALL view (c41_io_t * io_p)
{
  uint_t c, xs = 0;
  ssize_t z, max_page_size;
  size_t page_size;
  uint16_t h, w;
  int64_t file_size, page_ofs = 0;
  uint8_t clear_screen, get_screen_size, screen_too_small, quit = 0, line_len_changed;
  uint_t line_len, line_disp_len, ofs_len, i, j;
  acx1_event_t ev;
  uint8_t b, style;
  // uint8_t style, prev_style;
  uint8_t buf[0x100];
  static uint8_t data[0x10000];
  static uint8_t txt[HMAX][L];
  static uint8_t * (rtxt[HMAX]);
  uint_t tofs;
  ssize_t tlen;
  uint_t mll, nll;

  for (i = 0; i < HMAX; ++i) rtxt[i] = &txt[i][0];

  c = c41_io_get_size(io_p);
  if (c)
  {
    c41_sfmt(err_msg, sizeof(err_msg), "io_get_size");
    xs |= 1;
    return xs;
  }
  // z = c41_io_fmt(cli_p->stdout_p, "file size: $q\n", io_p->size);

  file_size = io_p->size;
  for (ofs_len = 4;
       ofs_len < 16 && (((int64_t) 1) << (ofs_len * 4)) < file_size;
       ofs_len += 2);

  clear_screen = get_screen_size = 1;
  line_len_changed = 0;
  max_page_size = 0;
  line_len = 0x100;
  //prev_style = STYLE__COUNT;
  for (; !quit;)
  {
    if (get_screen_size)
    {
      A(acx1_get_screen_size(&h, &w));
      get_screen_size = 0;
      if (w < 0x40 || h < 10) screen_too_small = 1;
      else
      {
        if (h > HMAX) h = HMAX;
        if (w > WMAX) w = WMAX;
        screen_too_small = 0;
        mll = ((w - ofs_len - 6) / 17) * 4;
        for (nll = 1; nll < mll; nll <<= 1);
        nll >>= 1;
        if (line_len > nll) { line_len = nll; line_len_changed = 1; }
      }
    }

    if (line_len_changed)
    {
      line_len_changed = 0;
      line_disp_len = ofs_len + 2 + (line_len / 4) * 17 + (line_len & 3) * 4 + ((line_len & 3) ? 1 : 0);
      max_page_size = (h - 2) * line_len;
    }

    if (clear_screen)
    {
      for (i = 0; i < h; ++i) txt[i][0] = 0;
      clear_screen = 0;
    }

    if (screen_too_small)
    {
      for (i = 0; i < h; ++i)
        c41_sfmt(txt[i], sizeof(txt[0]), "\a$cSCREEN TOO SMALL", STYLE_ERROR);
    }
    else
    {
      c = c41_io_p64read(io_p, data, page_ofs, max_page_size, &page_size);
      if (c && c != C41_IO_EOF) { xs |= 1; break; }

      c = acx1_set_cursor_mode(0); if (c) { xs |= 2; break; }

      c41_sfmt(txt[0], sizeof(txt[0]), "mode: page. view: [$Xq - $Xq]. file size: $Xq.",
               page_ofs, page_ofs + page_size - 1, file_size);

      for (i = 0; i < (uint_t) h - 2; ++i)
      {
        if (i * line_len >= page_size)
        {
          txt[i + 1][0] = 0;
          continue;
        }
        c41_sfmt(buf, sizeof(buf), "$.16Hq", page_ofs + i * line_len);
        tofs = c41_sfmt(txt[i + 1], L, "\a$c$s\a$c:", STYLE_OFFSET, &buf[16 - ofs_len], 
                 STYLE_SEPARATOR);

        for (j = 0; j < line_len; ++j)
        {
          tlen = c41_sfmt(txt[i + 1] + tofs, L - tofs, "\a$c$s",
                          STYLE_SEPARATOR, (!j || (j & 3) ? " " : "  "));
          tofs += tlen;
          if (i * line_len + j < page_size)
          {
            tlen = c41_sfmt(txt[i + 1] + tofs, L - tofs, "\a$c$.2UHb", 
                            STYLE_HEX, data[i * line_len + j]);
            tofs += tlen;
          }
          else
          {
            tlen = c41_sfmt(txt[i + 1] + tofs, L - tofs, "\a$c..", STYLE_NODATA);
            tofs += tlen;
          }
        }

        tlen = c41_sfmt(txt[i + 1] + tofs, L - tofs, "\a$c  ", STYLE_SEPARATOR);
        tofs += tlen;

        for (j = 0; j < line_len; ++j)
        {
          if (i * line_len + j >= page_size)
          {
            tlen = c41_sfmt(txt[i + 1] + tofs, L - tofs, "\a$c.", STYLE_NODATA);
            tofs += tlen;
          }
          else
          {
            b = data[i * line_len + j];
            if (b < 0x20) { style = STYLE_CHAR_CONTROL; b = b ? (b | 0x40) : '.'; }
            else if (b < 0x80)
            {
              if (b >= '0' && b <= '9') style = STYLE_CHAR_DIGIT;
              else if ((b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z'))
                style = STYLE_CHAR_LETTER;
              else if (b == 0x7F) { style = STYLE_CHAR_CONTROL; b = '/'; }
              else style = STYLE_CHAR_SYMBOL;
            }
            else if (b < 0xC0)
            {
              style = STYLE_CHAR_80_BF;
              b &= 0x3F; b = (b < 0x3F) ? (b | 0x40) : '/';
            }
            else
            {
              style = STYLE_CHAR_C0_FF;
              b &= 0x3F; b = (b < 0x3F) ? (b | 0x40) : '/';
            }
            tlen = c41_sfmt(txt[i + 1] + tofs, L - tofs, "\a$c$c", style, b);
            tofs += tlen;
          }
        }
      }
      if (xs) { break; }
    }
    c41_sfmt(txt[h - 1], L, "==");

    A(acx1_write_start());
    A(acx1_rect((uint8_t const * const *) rtxt, 1, 1, h, w, attrs));
    A(acx1_write_stop());

    c = acx1_read_event(&ev);
    if (c) { xs |= 2; break; }

    switch (ev.type)
    {
    case ACX1_RESIZE:
      get_screen_size = 1;
      break;
    case ACX1_KEY:
      if (ev.km == 'q' || ev.km == ('x' | ACX1_ALT) || ev.km == ACX1_F10)
        quit = 1;
      if (ev.km == ACX1_LEFT || ev.km == 'h') page_ofs -= 1;
      if (ev.km == ACX1_DOWN || ev.km == 'j') page_ofs += line_len;
      if (ev.km == ACX1_UP || ev.km == 'k') page_ofs -= line_len;
      if (ev.km == ACX1_RIGHT || ev.km == 'l') page_ofs += 1;
      if (ev.km == ' ' || ev.km == ACX1_PAGE_DOWN || ev.km == ('F' | ACX1_CTRL))
        page_ofs += line_len * (h - 3);
      if (ev.km == ('D' | ACX1_CTRL)) page_ofs += line_len * ((h - 3) / 3);
      if (ev.km == ACX1_PAGE_UP || ev.km == ('B' | ACX1_CTRL)) page_ofs -= line_len * (h - 3);
      if (ev.km == ('U' | ACX1_CTRL)) page_ofs -= line_len * ((h - 3) / 3);
      if (ev.km == (ACX1_PAGE_UP | ACX1_CTRL) || ev.km == 'g')
        page_ofs = 0;
      if (ev.km == (ACX1_PAGE_DOWN | ACX1_CTRL) || ev.km == 'G')
        page_ofs += file_size - file_size % line_len;
      if (ev.km == '<' && line_len > 1)
      {
        line_len -= 1;
        line_len_changed = 1;
      }
      if (ev.km == '>' && line_len < mll)
      {
        line_len += 1;
        line_len_changed = 1;
      }

      if (page_ofs + (h - 2) * line_len > file_size)
        page_ofs -= ((page_ofs - file_size) / line_len + h - 3) * line_len;
      if (page_ofs < 0) page_ofs = 0;
      break;
    }
  }

  while (!(xs & 2))
  {
    c = acx1_write_start(); if (c) { xs |= 2; break; }
    c = acx1_attr(0, 7, 0); if (c) { xs |= 2; break; }
    c = acx1_clear(); if (c) { xs |= 2; break; }
    c = acx1_write_stop(); if (c) { xs |= 2; break; }
    break;
  }

  return xs;
l_aerr:
  xs |= 2;
  return xs;
}

void init_attrs ()
{
  uint_t i, j;
  for (i = 0; i < C41_ITEM_COUNT(style); ++i)
  {
    j = style[i].style;
    attrs[j].bg = style[i].bg;
    attrs[j].fg = style[i].fg;
    attrs[j].mode = style[i].mode;
  }
}

/* hmain ********************************************************************/
uint8_t C41_CALL hmain (c41_cli_t * cli_p)
{
  c41_io_t * io_p;
  uint_t c;
  uint8_t xs;
  ssize_t z;
  char const * file_name;
  // log_file = fopen("ochi.log", "w");

  // acx1_logging(3, log_file);
  xs = 0;
  if (cli_p->arg_n != 1)
  {
    z = c41_io_fmt(cli_p->stderr_p, "Usage: ochi FILE\n");
    if (z < 0) return 2;
    return 1;
  }

  file_name = cli_p->arg_a[0];

  z = 1 + C41_STR_LEN(file_name);
  c = c41_file_open(cli_p->fsi_p, &io_p, (uint8_t const *) file_name, z,
                    C41_FSI_EXF_OPEN | C41_FSI_READ);
  if (c)
  {
    z = c41_io_fmt(cli_p->stderr_p, "Error: failed opening file $s ($s)\n",
                   file_name, c41_io_status_name(c));
    if (z < 0) xs |= 4;
    xs |= 1;
    return xs;
  }

  init_attrs();
  c = acx1_init();
  if (c) xs |= 2;
  else xs |= view(io_p);
  acx1_finish();

  c = c41_io_close(io_p);
  if (c)
  {
    z = c41_io_fmt(cli_p->stderr_p, "Error: failed closing file $s ($s)\n",
                   file_name, c41_io_status_name(c));
    xs |= 1;
  }
  if (xs)
  {
    c41_io_fmt(cli_p->stderr_p, "Error: $s\n", err_msg);
  }

  return xs;
}

