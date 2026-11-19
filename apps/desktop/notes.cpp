// What's new and Send feedback, the confirm prompt, the exit dialog and the close notice.
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

namespace {
// ---------------------------------------------------------------- confirm (mods/confirm)
// ------------------------------------------------------------------ What's new / feedback
// herdr app.whats_new (release notes, shown once after an update) and app.feedback (a draft the
// person reviews). AgentDesk has no server: the draft is copied or saved for them to send.
constexpr const char* kWhatsNew[] = {
    "A new look: sidebar with spaces and agents, tab bar with the window buttons, Files and Changes "
    "panels, picker, command palette, settings and welcome.",
    "Bigger interface text by default (15 px), adjustable in Settings > Look; Geist and JetBrains Mono are "
    "bundled.",
    "New tabs, spaces and splits open a shell in the focused folder; spaces and branches follow cd.",
    "WSL distributions are listed as shells, and Linux cd is followed too.",
    "Programs that read the mouse get clicks and the wheel; Shift keeps the mouse for selecting.",
    "Settle tab puts a running tab away until you show it or it needs you.",
    "Duplicate or reset a layout, duplicate a mod.",
};

}  // namespace

void draw_notes(App::State& s) {
    if (s.show_notes) {
        ImGui::OpenPopup("##notes");
        s.show_notes = false;
    }
    const bool feedback = s.notes_kind == "feedback";
    if (feedback) {
        // window.rs 3584: 680 wide; feedback/main.js 86: 650 high.
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const float fw = std::min(px(680), vp->WorkSize.x - px(16));
        const float fh = std::min(px(650), vp->WorkSize.y - px(16));
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + (vp->WorkSize.x - fw) * 0.5f, vp->WorkPos.y + (vp->WorkSize.y - fh) * 0.5f));
        ImGui::SetNextWindowSize(ImVec2(fw, fh));
    } else {
        center_next(px(520.0f), 0.16f);
    }
    push_overlay_style();
    if (feedback)
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const bool open = ImGui::BeginPopupModal("##notes", nullptr,
                                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                                 (feedback ? ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse : 0));
    if (feedback)
        ImGui::PopStyleVar();
    pop_overlay_style();
    if (!open)
        return;
    bool close = false;
    if (!feedback) {
        const std::string title = std::string("What's new in ") + AGENTDESK_VERSION_STRING;
        close = overlay_title(s, title.c_str());
    }
    if (!feedback) {
        for (const char* line : kWhatsNew) {
            ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->focus));
            ImGui::TextUnformatted("\xE2\x80\xA2");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextWrapped("%s", line);
            ImGui::Dummy(ImVec2(0, px(2)));
        }
        ImGui::Dummy(ImVec2(0, px(6)));
        const float bw = px(84);
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - bw - px(18));
        primary_button_style(true);
        close = ImGui::Button("Got it", ImVec2(bw, px(30))) || close;
        primary_button_style(false);
    } else {
        // feedback/main.js 86-112. AgentDesk has no server: Copy and Save to file stand in for
        // Send, and the line herdr shows when a build can't send says so.
        const ImVec2 wp = ImGui::GetWindowPos();
        const float ww = ImGui::GetWindowWidth();
        const float wh = ImGui::GetWindowHeight();
        ImDrawList* d = ImGui::GetWindowDrawList();
        // Title (98): px 24, py 18, 19 px semibold.
        const float title_h = px(18) + line_h(19) + px(18);
        d->AddText(s.fonts.ui_bold, fpx(19), ImVec2(wp.x + px(24), wp.y + px(18)), g_pal->text, "Send feedback");
        // 99: px 24, pb 12, 13 px accent.
        const char* kNoServer = "AgentDesk sends nothing by itself: copy the draft or save it to a file, then send it "
                                "the way you like.";
        float y = wp.y + title_h;
        {
            const char* t = kNoServer;
            const char* end = t + std::strlen(t);
            while (t < end) {
                const char* brk = s.fonts.ui->CalcWordWrapPosition(fpx(13), t, end, ww - px(48));
                if (brk == t)
                    brk = t + 1;
                d->AddText(s.fonts.ui, fpx(13), ImVec2(wp.x + px(24), y), g_pal->focus, t, brk);
                y += line_h(13);
                t = brk;
                while (t < end && *t == ' ')
                    ++t;
            }
            y += px(12);
        }
        OSVERSIONINFOEXW os{};
        os.dwOSVersionInfoSize = sizeof os;
        using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOEXW*);
        if (auto fn_ptr = reinterpret_cast<RtlGetVersionFn>(
                ::GetProcAddress(::GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")))
            fn_ptr(&os);
        const std::string about = std::string("AgentDesk ") + AGENTDESK_VERSION_STRING + ", Windows " +
                                  std::to_string(os.dwMajorVersion) + "." + std::to_string(os.dwMinorVersion) + "." +
                                  std::to_string(os.dwBuildNumber) + ", layout " + s.settings.layout;
        const std::string draft = std::string(s.feedback_buf) + "\n\n-- " + about + "\n";
        // Footer (110-112): px 24, py 16, gap 10, at the end: Cancel, then (ours) Save to file and
        // Copy, the primary. Buttons (61-66): px 14, py 7, rounded 7, a border; raised and muted,
        // or accent; a disabled primary looks like the others.
        const float btn_h = px(7) + line_h(13) + px(7) + 2.0f;
        const float foot_y = wp.y + wh - px(16) - btn_h;
        const float note_h = s.feedback_note.empty() ? 0.0f : px(10) + line_h(13);
        // Body (100-108): px 24, gap 10, scrolls between the title and the footer.
        ImGui::SetCursorScreenPos(ImVec2(wp.x, y));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(24), 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(px(10), 0));
        ImGui::BeginChild("##fb-body", ImVec2(ww, std::max(px(40), foot_y - px(16) - note_h - y)),
                          ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground);
        ImDrawList* bd = ImGui::GetWindowDrawList();
        const float bw_full = ImGui::GetContentRegionAvail().x;
        auto label = [&](const char* text) {  // 95: 13 px muted
            const ImVec2 p = ImGui::GetCursorScreenPos();
            bd->AddText(s.fonts.ui, fpx(13), p, g_pal->dim, text);
            ImGui::Dummy(ImVec2(bw_full, line_h(13) + px(10)));
        };
        label("Your message (up to 4000 characters).");
        // 96, 104: p 10, rounded 7, a border, the background, 14 px; 130 high.
        {
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::PushFont(s.fonts.ui, 14);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(10), px(10)));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(7));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, v4(g_pal->bg));
            ImGui::PushStyleColor(ImGuiCol_Border, v4(g_pal->border));
            ImGui::InputTextMultiline("##fb", s.feedback_buf, std::min<std::size_t>(sizeof s.feedback_buf, 4001),
                                      ImVec2(bw_full, px(130)));
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(3);
            ImGui::PopFont();
            // 9: the placeholder.
            if (s.feedback_buf[0] == '\0')
                bd->AddText(s.fonts.ui, fpx(14), ImVec2(p.x + px(10), p.y + px(10)), g_pal->faint,
                            "What happened? What did you expect?");
            ImGui::Dummy(ImVec2(0, px(10)));
        }
        label("What gets copied or saved: exactly this, nothing else.");
        // 107-108: p 10, rounded 7, a border, the background; 11 px lines of 15 px, dim.
        {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            int lines = 1;
            for (const char ch : draft)
                lines += ch == '\n' ? 1 : 0;
            const float bh = px(10) * 2 + px(15) * static_cast<float>(lines);
            bd->AddRectFilled(p, ImVec2(p.x + bw_full, p.y + bh), g_pal->bg, px(7));
            bd->AddRect(p, ImVec2(p.x + bw_full, p.y + bh), g_pal->border, px(7));
            bd->PushClipRect(p, ImVec2(p.x + bw_full - px(10), p.y + bh), true);
            float ly = p.y + px(10);
            for (std::size_t at = 0; at <= draft.size();) {
                const auto nl = draft.find('\n', at);
                const std::string line = draft.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
                bd->AddText(s.fonts.ui, fpx(11), ImVec2(p.x + px(10), ly + (px(15) - fpx(11)) * 0.5f), g_pal->faint,
                            line.empty() ? " " : line.c_str());
                ly += px(15);
                if (nl == std::string::npos)
                    break;
                at = nl + 1;
            }
            bd->PopClipRect();
            ImGui::Dummy(ImVec2(bw_full, bh));
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        // 109: px 24, pt 10, 13 px accent (the note after Copy or Save).
        if (!s.feedback_note.empty())
            d->AddText(s.fonts.ui, fpx(13), ImVec2(wp.x + px(24), foot_y - px(16) - line_h(13)), g_pal->focus,
                       s.feedback_note.c_str());
        const bool empty = s.feedback_buf[0] == '\0';
        auto button = [&](const char* id, const char* text, bool primary, bool disabled, float x) {
            const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, text);
            const ImVec2 a(x, foot_y);
            const ImVec2 sz(t.x + px(28) + 2.0f, btn_h);
            ImGui::SetCursorScreenPos(a);
            const bool hit = ImGui::InvisibleButton(id, sz) && !disabled;
            const bool on = primary && !disabled;
            d->AddRectFilled(a, ImVec2(a.x + sz.x, a.y + sz.y), on ? g_pal->focus : raised_color(), px(7));
            d->AddRect(a, ImVec2(a.x + sz.x, a.y + sz.y), g_pal->border, px(7));
            d->AddText(s.fonts.ui, fpx(13), ImVec2(a.x + px(14) + 1.0f, a.y + (sz.y - t.y) * 0.5f),
                       on ? accent_foreground() : g_pal->dim, text);
            return hit;
        };
        auto width_of = [&](const char* text) {
            return s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, text).x + px(28) + 2.0f;
        };
        float bx = wp.x + ww - px(24) - width_of("Copy");
        const float copy_x = bx;
        bx -= px(10) + width_of("Save to file");
        const float save_x = bx;
        bx -= px(10) + width_of("Cancel");
        close = button("##fb-cancel", "Cancel", false, false, bx) || close;
        if (button("##fb-save", "Save to file", false, empty, save_x)) {
            const auto dir = data_dir() / L"feedback";
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            SYSTEMTIME t{};
            ::GetLocalTime(&t);
            wchar_t name[64]{};
            std::swprintf(name, 64, L"feedback-%04u%02u%02u-%02u%02u%02u.md", t.wYear, t.wMonth, t.wDay, t.wHour,
                          t.wMinute, t.wSecond);
            std::ofstream(dir / name, std::ios::binary) << draft;
            ::ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            s.feedback_note = "Saved in the feedback folder.";
        }
        if (button("##fb-copy", "Copy", true, empty, copy_x)) {
            ImGui::SetClipboardText(draft.c_str());
            s.feedback_note = "Copied. Paste it where you want to send it.";
        }
    }
    if (close || ImGui::IsKeyPressed(ImGuiKey_Escape) || dialog_outside_click()) {
        ImGui::CloseCurrentPopup();
        s.focus_view(s.ws.focused_session());
    }
    ImGui::EndPopup();
}

void draw_confirm(App::State& s) {
    if (s.show_confirm) {
        ImGui::OpenPopup("##confirm");
        s.show_confirm = false;
    }
    center_next(px(380), 0.24f);
    push_overlay_style();
    const bool open = ImGui::BeginPopupModal("##confirm", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
    pop_overlay_style();
    if (!open)
        return;
    bool cancel = overlay_title(s, s.confirm_title.c_str(), EscChip::kPlain);  // confirm/main.js 42
    if (!s.confirm_detail.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->dim));
        ImGui::TextWrapped("%s", s.confirm_detail.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Dummy(ImVec2(0, px(8)));
    const float bw = px(84);
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - bw * 2 - px(26));
    ImGui::PushStyleColor(ImGuiCol_Button, v4(raised_color()));
    cancel = ImGui::Button("Cancel", ImVec2(bw, px(30))) || cancel || ImGui::IsKeyPressed(ImGuiKey_Escape) || dialog_outside_click();
    ImGui::PopStyleColor();
    ImGui::SameLine();
    primary_button_style(true);
    // herdr confirm/main.js: Cancel and Confirm.
    const bool ok = ImGui::Button("Confirm", ImVec2(bw, px(30))) || ImGui::IsKeyPressed(ImGuiKey_Enter, false);
    primary_button_style(false);
    if (cancel)
        ImGui::CloseCurrentPopup();
    if (ok) {
        ImGui::CloseCurrentPopup();
        run_command(s, s.confirm_command);
    }
    ImGui::EndPopup();
}

void draw_exit(App::State& s) {
    if (s.show_exit) {
        ImGui::OpenPopup("##exit");
        s.show_exit = false;
    }
    center_next(px(520), 0.2f);
    if (!ImGui::BeginPopupModal("##exit", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize))
        return;
    heading(s, "Exit and stop all sessions");
    ImGui::TextDisabled("These sessions will be force-stopped and agent-host will exit:");
    int running = 0;
    for (const auto& sess : s.sessions) {
        const auto& v = s.views_by_id[sess->id()];
        if (v.process_state != "Running" && v.process_state != "Stopping" && v.process_state != "Starting")
            continue;
        ++running;
        ImGui::BulletText("%s  (%s)  %s", s.pane_title(sess->id()).c_str(), v.process_state.c_str(),
                          display_path(v.cwd).c_str());
    }
    if (running == 0)
        ImGui::TextDisabled("(no running sessions)");
    std::string error;
    bool pending = false;
    {
        std::lock_guard lock(s.placements_mutex);
        error = s.exit_error;
        pending = s.exit_pending;
    }
    if (!error.empty())
        ImGui::TextColored(v4(g_pal->needs_you), "%s", error.c_str());
    ImGui::PushStyleColor(ImGuiCol_Button, v4(alpha(g_pal->needs_you, 210)));
    ImGui::PushStyleColor(ImGuiCol_Text, v4(IM_COL32(255, 255, 255, 255)));
    // One request at a time: the answer comes on a HostClient thread (exit_now, or the error).
    ImGui::BeginDisabled(pending);
    if (ImGui::Button(pending ? "Stopping\xE2\x80\xA6" : "Stop all and exit")) {
        {
            std::lock_guard lock(s.placements_mutex);
            s.exit_pending = true;
            s.exit_error.clear();
        }
        s.host->shutdown_host(true, [st = &s, wake = s.config.wake](std::string e) {
            {
                std::lock_guard lock(st->placements_mutex);
                st->exit_pending = false;
                if (!e.empty())
                    st->exit_error = e;
            }
            if (e.empty())
                st->exit_now = true;
            if (wake)
                wake();
        });
    }
    ImGui::EndDisabled();
    ImGui::PopStyleColor(2);
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void draw_close_notice(App::State& s) {
    if (s.show_close_notice) {
        ImGui::OpenPopup("##closenotice");
        s.show_close_notice = false;
    }
    center_next(px(520), 0.25f);
    if (!ImGui::BeginPopupModal("##closenotice", nullptr,
                                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize))
        return;
    heading(s, "Sessions keep running");
    ImGui::TextWrapped(
        "Closing this window only detaches. Your sessions keep running in agent-host, and reopening "
        "AgentDesk "
        "brings back the same spaces, tabs and panes. Use \"Exit and stop all\" in the sidebar to end "
        "everything.");
    primary_button_style(true);
    if (ImGui::Button("Close window")) {
        s.settings.close_notice_shown = true;  // shown once (design doc 4.3)
        save_settings(s.settings);
        s.exit_now = true;
    }
    primary_button_style(false);
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}
}  // namespace detail

}  // namespace agentdesk::desktop
