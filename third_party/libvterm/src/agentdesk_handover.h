/* AgentDesk addition (not upstream): what agentdesk_handover.c needs from screen.c, whose
 * cell and pen types are private to that file. See include/vterm_agentdesk.h. */
#ifndef __VTERM_AGENTDESK_HANDOVER_H__
#define __VTERM_AGENTDESK_HANDOVER_H__

#include "vterm_internal.h"

/* One screen cell (or the screen's pen), field by field. */
typedef struct {
  uint32_t chars[VTERM_MAX_CHARS_PER_CELL];
  VTermColor fg, bg;
  unsigned char bold, underline, italic, blink, reverse, conceal, strike, font, small, baseline;
  unsigned char protected_cell, dwl, dhl;
} AgentDeskCell;

int  agentdesk_screen_has_buffer(const VTermScreen *screen, int bufidx);
int  agentdesk_screen_selected(const VTermScreen *screen); /* 1: the alternate buffer is shown */
void agentdesk_screen_select(VTermScreen *screen, int bufidx);
void agentdesk_screen_get_cell(const VTermScreen *screen, int bufidx, int index, AgentDeskCell *out);
void agentdesk_screen_set_cell(VTermScreen *screen, int bufidx, int index, const AgentDeskCell *in);
void agentdesk_screen_get_pen(const VTermScreen *screen, AgentDeskCell *out);
void agentdesk_screen_set_pen(VTermScreen *screen, const AgentDeskCell *in);
int  agentdesk_screen_get_flags(const VTermScreen *screen); /* bit 0 global_reverse, bit 1 reflow */
void agentdesk_screen_set_flags(VTermScreen *screen, int flags);

#endif
