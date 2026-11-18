/* AgentDesk addition to libvterm 0.3.3 (not upstream; see README.agentdesk.md).
 *
 * Moves a terminal's complete libvterm state between two processes, for agent-host's
 * self-update (design doc 7.4 OQ-09): the old host saves it, the new host loads it into a
 * VTerm of the same size, and parsing continues with the next byte as if nothing happened.
 *
 * Only at a byte boundary where the parser is in its ground state: no escape sequence, CSI,
 * OSC, DCS or string is half read (vterm_agentdesk_at_ground). Fields are written one by one
 * with a format number, never as raw structs, so a newer build can refuse a format it does not
 * know instead of misreading it.
 */
#ifndef __VTERM_AGENTDESK_H__
#define __VTERM_AGENTDESK_H__

#include <stddef.h>

#include "vterm.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VTERM_AGENTDESK_FORMAT 1

/* 1 when the next byte starts fresh: the parser is in its ground state. */
int vterm_agentdesk_at_ground(const VTerm *vt);

/* The state of `vt` (parser at ground) as bytes. Returns the length and sets *out to a
 * malloc()ed buffer the caller free()s; 0 when not at ground or out of memory. */
size_t vterm_agentdesk_save(VTerm *vt, char **out);

/* Loads what vterm_agentdesk_save wrote into `vt`, which must have been created with the same
 * size, set up with vterm_obtain_screen + vterm_screen_enable_altscreen(1) +
 * vterm_screen_reset. Returns 1, or 0 when the data is not a state of this format and size
 * (then `vt` must be discarded). No callbacks run. */
int vterm_agentdesk_load(VTerm *vt, const char *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif
