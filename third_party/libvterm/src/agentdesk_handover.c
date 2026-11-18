/* AgentDesk addition (not upstream): saves and loads a VTerm's whole state for agent-host's
 * self-update. See include/vterm_agentdesk.h. Every field is written one at a time, little
 * endian; nothing is copied as a raw struct except VTermColor's four bytes. */
#include "vterm_agentdesk.h"

#include <stdlib.h>
#include <string.h>

#include "agentdesk_handover.h"

#define MAGIC 0x44415456u /* "VTAD" */

typedef char vterm_color_is_4_bytes[sizeof(VTermColor) == 4 ? 1 : -1];
typedef char encoding_data_is_16_bytes[sizeof(((VTermEncodingInstance *)0)->data) == 16 ? 1 : -1];

/* ---- writer */

typedef struct {
  char *buf;
  size_t len, cap;
  int failed;
} Out;

static void put(Out *o, const void *p, size_t n)
{
  if(o->failed)
    return;
  if(o->len + n > o->cap) {
    size_t cap = o->cap ? o->cap * 2 : 4096;
    while(cap < o->len + n)
      cap *= 2;
    char *b = realloc(o->buf, cap);
    if(!b) {
      o->failed = 1;
      return;
    }
    o->buf = b;
    o->cap = cap;
  }
  memcpy(o->buf + o->len, p, n);
  o->len += n;
}

static void put_u32(Out *o, uint32_t v)
{
  unsigned char b[4] = { v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff, (v >> 24) & 0xff };
  put(o, b, 4);
}

static void put_i32(Out *o, int v) { put_u32(o, (uint32_t)v); }
static void put_u8(Out *o, unsigned v) { unsigned char b = (unsigned char)v; put(o, &b, 1); }
static void put_color(Out *o, const VTermColor *c) { put(o, c, 4); }

/* ---- reader */

typedef struct {
  const unsigned char *p;
  size_t left;
  int failed;
} In;

static void get(In *in, void *dst, size_t n)
{
  if(in->failed || in->left < n) {
    in->failed = 1;
    memset(dst, 0, n);
    return;
  }
  memcpy(dst, in->p, n);
  in->p += n;
  in->left -= n;
}

static uint32_t get_u32(In *in)
{
  unsigned char b[4];
  get(in, b, 4);
  return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static int get_i32(In *in) { return (int)get_u32(in); }
static unsigned get_u8(In *in) { unsigned char b; get(in, &b, 1); return b; }
static void get_color(In *in, VTermColor *c) { get(in, c, 4); }

/* ---- pieces */

static void put_pen(Out *o, const struct VTermPen *pen)
{
  put_color(o, &pen->fg);
  put_color(o, &pen->bg);
  put_u8(o, pen->bold);
  put_u8(o, pen->underline);
  put_u8(o, pen->italic);
  put_u8(o, pen->blink);
  put_u8(o, pen->reverse);
  put_u8(o, pen->conceal);
  put_u8(o, pen->strike);
  put_u8(o, pen->font);
  put_u8(o, pen->small);
  put_u8(o, pen->baseline);
}

static void get_pen(In *in, struct VTermPen *pen)
{
  get_color(in, &pen->fg);
  get_color(in, &pen->bg);
  pen->bold = get_u8(in);
  pen->underline = get_u8(in);
  pen->italic = get_u8(in);
  pen->blink = get_u8(in);
  pen->reverse = get_u8(in);
  pen->conceal = get_u8(in);
  pen->strike = get_u8(in);
  pen->font = get_u8(in);
  pen->small = get_u8(in);
  pen->baseline = get_u8(in);
}

static void put_cell(Out *o, const AgentDeskCell *c)
{
  for(int i = 0; i < VTERM_MAX_CHARS_PER_CELL; i++)
    put_u32(o, c->chars[i]);
  put_color(o, &c->fg);
  put_color(o, &c->bg);
  const unsigned char bits[13] = { c->bold, c->underline, c->italic, c->blink, c->reverse, c->conceal,
                                   c->strike, c->font, c->small, c->baseline, c->protected_cell, c->dwl,
                                   c->dhl };
  put(o, bits, sizeof bits);
}

static void get_cell(In *in, AgentDeskCell *c)
{
  for(int i = 0; i < VTERM_MAX_CHARS_PER_CELL; i++)
    c->chars[i] = get_u32(in);
  get_color(in, &c->fg);
  get_color(in, &c->bg);
  unsigned char bits[13];
  get(in, bits, sizeof bits);
  c->bold = bits[0]; c->underline = bits[1]; c->italic = bits[2]; c->blink = bits[3];
  c->reverse = bits[4]; c->conceal = bits[5]; c->strike = bits[6]; c->font = bits[7];
  c->small = bits[8]; c->baseline = bits[9]; c->protected_cell = bits[10]; c->dwl = bits[11];
  c->dhl = bits[12];
}

/* An encoding is saved by how it was designated: 'u' UTF-8, else the ESC ( final byte. */
static VTermEncoding *encoding_for(unsigned type, unsigned designation)
{
  return vterm_lookup_encoding(type == 0 ? ENC_UTF8 : ENC_SINGLE_94, (char)designation);
}

static int put_encoding(Out *o, const VTermEncodingInstance *e)
{
  if(e->enc == vterm_lookup_encoding(ENC_UTF8, 'u')) {
    put_u8(o, 0);
    put_u8(o, 'u');
  }
  else {
    unsigned found = 0;
    for(unsigned d = 0x20; d < 0x7f && !found; d++)
      if(vterm_lookup_encoding(ENC_SINGLE_94, (char)d) == e->enc)
        found = d;
    if(!found)
      return 0;
    put_u8(o, 1);
    put_u8(o, found);
  }
  put(o, e->data, sizeof e->data);
  return 1;
}

static int get_encoding(In *in, VTermEncodingInstance *e)
{
  const unsigned type = get_u8(in);
  const unsigned designation = get_u8(in);
  VTermEncoding *enc = encoding_for(type, designation);
  if(!enc)
    return 0;
  e->enc = enc;
  get(in, e->data, sizeof e->data);
  return 1;
}

static void put_lineinfo(Out *o, const VTermLineInfo *li, int rows)
{
  put_u8(o, li != NULL);
  if(!li)
    return;
  for(int r = 0; r < rows; r++) {
    put_u8(o, li[r].doublewidth);
    put_u8(o, li[r].doubleheight);
    put_u8(o, li[r].continuation);
  }
}

static int get_lineinfo(In *in, VTermLineInfo *li, int rows)
{
  const unsigned present = get_u8(in);
  if(!present)
    return 1; /* nothing saved: keep the fresh (cleared) lines */
  for(int r = 0; r < rows; r++) {
    const unsigned dw = get_u8(in), dh = get_u8(in), cont = get_u8(in);
    if(li) {
      li[r].doublewidth = dw;
      li[r].doubleheight = dh;
      li[r].continuation = cont;
    }
  }
  return li != NULL;
}

/* ---- API */

int vterm_agentdesk_at_ground(const VTerm *vt)
{
  return vt->parser.state == NORMAL && !vt->parser.in_esc && vt->parser.intermedlen == 0;
}

size_t vterm_agentdesk_save(VTerm *vt, char **out)
{
  *out = NULL;
  if(!vterm_agentdesk_at_ground(vt) || !vt->state || !vt->screen)
    return 0;
  const VTermState *s = vt->state;
  const VTermScreen *screen = vt->screen;
  const int rows = vt->rows, cols = vt->cols;
  Out o = { 0 };

  put_u32(&o, MAGIC);
  put_u32(&o, VTERM_AGENTDESK_FORMAT);
  put_i32(&o, rows);
  put_i32(&o, cols);

  /* VTerm */
  put_u8(&o, vt->mode.utf8);
  put_u8(&o, vt->mode.ctrl8bit);
  put_u8(&o, vt->parser.string_initial);
  put_u8(&o, vt->parser.emit_nul);

  /* VTermState */
  put_i32(&o, s->pos.row);
  put_i32(&o, s->pos.col);
  put_i32(&o, s->at_phantom);
  put_i32(&o, s->scrollregion_top);
  put_i32(&o, s->scrollregion_bottom);
  put_i32(&o, s->scrollregion_left);
  put_i32(&o, s->scrollregion_right);
  put(&o, s->tabstops, (size_t)(cols + 7) / 8);
  put_lineinfo(&o, s->lineinfos[BUFIDX_PRIMARY], rows);
  put_lineinfo(&o, s->lineinfos[BUFIDX_ALTSCREEN], rows);
  put_u8(&o, s->lineinfo == s->lineinfos[BUFIDX_ALTSCREEN] && s->lineinfos[BUFIDX_ALTSCREEN] != NULL);
  put_i32(&o, s->mouse_col);
  put_i32(&o, s->mouse_row);
  put_i32(&o, s->mouse_buttons);
  put_i32(&o, s->mouse_flags);
  put_i32(&o, (int)s->mouse_protocol);
  put_u32(&o, (uint32_t)s->combine_chars_size);
  for(size_t i = 0; i < s->combine_chars_size; i++)
    put_u32(&o, s->combine_chars[i]);
  put_i32(&o, s->combine_width);
  put_i32(&o, s->combine_pos.row);
  put_i32(&o, s->combine_pos.col);
  const unsigned modes[15] = { s->mode.keypad, s->mode.cursor, s->mode.autowrap, s->mode.insert,
                               s->mode.newline, s->mode.cursor_visible, s->mode.cursor_blink,
                               s->mode.cursor_shape, s->mode.alt_screen, s->mode.origin, s->mode.screen,
                               s->mode.leftrightmargin, s->mode.bracketpaste, s->mode.report_focus, 0 };
  for(int i = 0; i < 15; i++)
    put_u8(&o, modes[i]);
  for(int i = 0; i < 4; i++)
    if(!put_encoding(&o, &s->encoding[i])) {
      free(o.buf);
      return 0;
    }
  if(!put_encoding(&o, &s->encoding_utf8)) {
    free(o.buf);
    return 0;
  }
  put_i32(&o, s->gl_set);
  put_i32(&o, s->gr_set);
  put_i32(&o, s->gsingle_set);
  put_pen(&o, &s->pen);
  put_color(&o, &s->default_fg);
  put_color(&o, &s->default_bg);
  for(int i = 0; i < 16; i++)
    put_color(&o, &s->colors[i]);
  put_i32(&o, s->bold_is_highbright);
  put_u8(&o, s->protected_cell);
  put_i32(&o, s->saved.pos.row);
  put_i32(&o, s->saved.pos.col);
  put_pen(&o, &s->saved.pen);
  put_u8(&o, s->saved.mode.cursor_visible);
  put_u8(&o, s->saved.mode.cursor_blink);
  put_u8(&o, s->saved.mode.cursor_shape);

  /* VTermScreen */
  put_u8(&o, (unsigned)agentdesk_screen_get_flags(screen));
  AgentDeskCell cell;
  agentdesk_screen_get_pen(screen, &cell);
  put_cell(&o, &cell);
  for(int b = 0; b < 2; b++) {
    const int has = agentdesk_screen_has_buffer(screen, b);
    put_u8(&o, has);
    if(!has)
      continue;
    for(int i = 0; i < rows * cols; i++) {
      agentdesk_screen_get_cell(screen, b, i, &cell);
      put_cell(&o, &cell);
    }
  }
  put_u8(&o, agentdesk_screen_selected(screen));
  put_u32(&o, MAGIC);

  if(o.failed) {
    free(o.buf);
    return 0;
  }
  *out = o.buf;
  return o.len;
}

int vterm_agentdesk_load(VTerm *vt, const char *data, size_t len)
{
  In in = { (const unsigned char *)data, len, 0 };
  VTermState *s = vt->state;
  VTermScreen *screen = vt->screen;
  const int rows = vt->rows, cols = vt->cols;
  if(!s || !screen)
    return 0;
  if(get_u32(&in) != MAGIC || get_u32(&in) != VTERM_AGENTDESK_FORMAT)
    return 0;
  if(get_i32(&in) != rows || get_i32(&in) != cols || in.failed)
    return 0;

  vt->mode.utf8 = get_u8(&in);
  vt->mode.ctrl8bit = get_u8(&in);
  vt->parser.state = NORMAL;
  vt->parser.in_esc = false;
  vt->parser.intermedlen = 0;
  vt->parser.string_initial = get_u8(&in) != 0;
  vt->parser.emit_nul = get_u8(&in) != 0;

  s->pos.row = get_i32(&in);
  s->pos.col = get_i32(&in);
  s->at_phantom = get_i32(&in);
  s->scrollregion_top = get_i32(&in);
  s->scrollregion_bottom = get_i32(&in);
  s->scrollregion_left = get_i32(&in);
  s->scrollregion_right = get_i32(&in);
  get(&in, s->tabstops, (size_t)(cols + 7) / 8);
  if(!get_lineinfo(&in, s->lineinfos[BUFIDX_PRIMARY], rows))
    return 0;
  if(!get_lineinfo(&in, s->lineinfos[BUFIDX_ALTSCREEN], rows))
    return 0;
  const unsigned alt_lines = get_u8(&in);
  if(alt_lines && !s->lineinfos[BUFIDX_ALTSCREEN])
    return 0;
  s->lineinfo = s->lineinfos[alt_lines ? BUFIDX_ALTSCREEN : BUFIDX_PRIMARY];
  s->mouse_col = get_i32(&in);
  s->mouse_row = get_i32(&in);
  s->mouse_buttons = get_i32(&in);
  s->mouse_flags = get_i32(&in);
  s->mouse_protocol = get_i32(&in);
  const uint32_t combine_size = get_u32(&in);
  if(in.failed || combine_size == 0 || combine_size > in.left / 4)
    return 0;
  if(combine_size != s->combine_chars_size) {
    uint32_t *chars = vterm_allocator_malloc(vt, combine_size * sizeof(chars[0]));
    if(!chars)
      return 0;
    vterm_allocator_free(vt, s->combine_chars);
    s->combine_chars = chars;
    s->combine_chars_size = combine_size;
  }
  for(uint32_t i = 0; i < combine_size; i++)
    s->combine_chars[i] = get_u32(&in);
  s->combine_width = get_i32(&in);
  s->combine_pos.row = get_i32(&in);
  s->combine_pos.col = get_i32(&in);
  unsigned modes[15];
  for(int i = 0; i < 15; i++)
    modes[i] = get_u8(&in);
  s->mode.keypad = modes[0];
  s->mode.cursor = modes[1];
  s->mode.autowrap = modes[2];
  s->mode.insert = modes[3];
  s->mode.newline = modes[4];
  s->mode.cursor_visible = modes[5];
  s->mode.cursor_blink = modes[6];
  s->mode.cursor_shape = modes[7];
  s->mode.alt_screen = modes[8];
  s->mode.origin = modes[9];
  s->mode.screen = modes[10];
  s->mode.leftrightmargin = modes[11];
  s->mode.bracketpaste = modes[12];
  s->mode.report_focus = modes[13];
  for(int i = 0; i < 4; i++)
    if(!get_encoding(&in, &s->encoding[i]))
      return 0;
  if(!get_encoding(&in, &s->encoding_utf8))
    return 0;
  s->gl_set = get_i32(&in);
  s->gr_set = get_i32(&in);
  s->gsingle_set = get_i32(&in);
  if(s->gl_set < 0 || s->gl_set > 3 || s->gr_set < 0 || s->gr_set > 3 || s->gsingle_set < 0 || s->gsingle_set > 3)
    return 0;
  get_pen(&in, &s->pen);
  get_color(&in, &s->default_fg);
  get_color(&in, &s->default_bg);
  for(int i = 0; i < 16; i++)
    get_color(&in, &s->colors[i]);
  s->bold_is_highbright = get_i32(&in);
  s->protected_cell = get_u8(&in);
  s->saved.pos.row = get_i32(&in);
  s->saved.pos.col = get_i32(&in);
  get_pen(&in, &s->saved.pen);
  s->saved.mode.cursor_visible = get_u8(&in);
  s->saved.mode.cursor_blink = get_u8(&in);
  s->saved.mode.cursor_shape = get_u8(&in);

  agentdesk_screen_set_flags(screen, (int)get_u8(&in));
  AgentDeskCell cell;
  get_cell(&in, &cell);
  agentdesk_screen_set_pen(screen, &cell);
  for(int b = 0; b < 2; b++) {
    const unsigned has = get_u8(&in);
    if(!has)
      continue;
    if(!agentdesk_screen_has_buffer(screen, b))
      return 0;
    for(int i = 0; i < rows * cols; i++) {
      get_cell(&in, &cell);
      if(in.failed)
        return 0;
      agentdesk_screen_set_cell(screen, b, i, &cell);
    }
  }
  const unsigned alt = get_u8(&in);
  if(alt && !agentdesk_screen_has_buffer(screen, BUFIDX_ALTSCREEN))
    return 0;
  agentdesk_screen_select(screen, alt ? BUFIDX_ALTSCREEN : BUFIDX_PRIMARY);
  if(get_u32(&in) != MAGIC || in.failed || in.left != 0)
    return 0;
  if(s->pos.row < 0 || s->pos.row >= rows || s->pos.col < 0 || s->pos.col > cols)
    return 0;
  return 1;
}
