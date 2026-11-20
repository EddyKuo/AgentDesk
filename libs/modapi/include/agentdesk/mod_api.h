/* AgentDesk mod interface, version 1 (ADR-17).
 *
 * A mod is a DLL at <data>\mods\<name>\mod.dll (optional mod.json {"description": "..."} and
 * settings.json next to it). AgentDesk loads it only when it is enabled in Settings > Mods and
 * calls agentdesk_mod_init once, on the UI thread. Every call into the mod happens on the UI
 * thread; keep them short. All strings are UTF-8 and NUL-terminated.
 *
 * Plain C so a mod built with another compiler or runtime still links: nothing but pointers,
 * ints and C strings crosses the boundary; structured data is JSON text. C++ mods use
 * agentdesk/mod_sdk.hpp instead of filling these structs by hand.
 *
 * A mod runs inside agent-ui.exe with the person's rights, like any program they install.
 * AgentDesk guards each call: a mod that crashes (an access violation, an escaping exception)
 * is turned off with an error in Settings > Mods, and its DLL stays loaded until AgentDesk
 * restarts. A mod that never returns (an endless loop, a blocking wait) freezes the window: the
 * terminals keep running in agent-host, but agent-ui has to be ended and started again.
 *
 * Compatibility: a new interface version only appends members at the end of these structs and
 * never changes or removes existing ones. AgentDesk loads a mod whose api_version is between 1
 * and its own AGENTDESK_MOD_API_VERSION; members the mod does not know stay zero/NULL. A mod
 * built against a newer header than the running AgentDesk is refused with an error.
 */
#ifndef AGENTDESK_MOD_API_H
#define AGENTDESK_MOD_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AGENTDESK_MOD_API_VERSION 1u

/* What AgentDesk offers the mod. Requests take effect after the mod's call returns. */
typedef struct agentdesk_host {
    uint32_t api_version;
    void* ctx; /* pass back as the first argument */

    /* Runs a window command ("tab.new", "agent.focus:<session>", "layout:terminals", ...). */
    void (*run)(void* ctx, const char* command);
    /* Types text into a session; enter != 0 presses Enter after it. */
    void (*send)(void* ctx, const char* session, const char* text, int enter);
    /* Shows a notification in the corner. body may be NULL. */
    void (*notify)(void* ctx, const char* title, const char* body);
    /* Starts a program directly (no shell). argv[0] is looked up on PATH. At most 64 arguments. */
    void (*exec)(void* ctx, const char* const* argv, int argc);
    /* Adds an entry to the command palette; picking it calls agentdesk_mod.command(id). */
    void (*add_command)(void* ctx, const char* id, const char* label);
    /* JSON array of sessions: [{id, title, program, cwd, state, process, machine, parent,
     * created_at, labels, focused}]. Valid until the mod's current call returns. */
    const char* (*sessions)(void* ctx);
    /* The value of `key` in the mod's settings.json as JSON text, or NULL when it is not set.
     * Valid until the mod's current call returns. */
    const char* (*setting)(void* ctx, const char* key);
} agentdesk_host;

/* What the mod offers. Any callback may be NULL. Returned strings stay owned by the mod and
 * must remain valid until the next call into the mod. */
typedef struct agentdesk_mod {
    uint32_t api_version; /* set to AGENTDESK_MOD_API_VERSION */
    void* self;           /* passed back as the first argument */
    const char* description;

    /* Events: "agent_state" {session, state, previous, title}, "session_added" {session},
     * "session_exited" {session, exit_code}, "tick" {} (about once a second). */
    void (*on_event)(void* self, const char* event, const char* payload_json);
    /* Panels for `where` ("sidebar" or "details") as a JSON array:
     * [{"id", "title", "widgets": [{"kind": "text|button|separator|progress|row|badge",
     *   "text", "text2", "color": "|dim|accent|good|warn|bad", "value": 0..1, "id"}]}] */
    const char* (*panels)(void* self, const char* where);
    /* A button in one of the mod's panels was clicked. */
    void (*click)(void* self, const char* panel, const char* widget);
    /* Text after a tab title (kind "tab", subject {title, sessions: [ids]}) or an agent row
     * (kind "agent", subject a session). Return "" or NULL for nothing. */
    const char* (*decorate)(void* self, const char* kind, const char* subject_json);
    /* A palette entry added with add_command was picked. */
    void (*command)(void* self, const char* id);
    /* Called once before the DLL is released (reload, AgentDesk exit). */
    void (*destroy)(void* self);
} agentdesk_mod;

/* Exported by every mod DLL as "agentdesk_mod_init". Fill *mod and return 0, or return
 * non-zero to refuse loading. `host` stays valid until destroy. */
typedef int (*agentdesk_mod_init_fn)(const agentdesk_host* host, agentdesk_mod* mod);

#ifdef __cplusplus
}
#endif

#endif /* AGENTDESK_MOD_API_H */
