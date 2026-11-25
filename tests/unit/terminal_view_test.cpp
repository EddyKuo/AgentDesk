// TerminalView drawn without a window: what a mouse drag paints.
#include "agentdesk/ui/terminal_view.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <memory>

#include <gtest/gtest.h>
#include <imgui.h>

using namespace agentdesk;

namespace {

constexpr ImU32 kSelection = IM_COL32(1, 2, 3, 77);  // a colour nothing else is drawn in

class FixedScreen : public ui::TerminalSource {
public:
    explicit FixedScreen(const char* bytes) {
        terminal::VtEngineOptions o;
        o.rows = 4;
        o.cols = 20;
        terminal::VtEngine e(o);
        e.feed(bytes);
        snap_ = std::make_shared<const terminal::ScreenSnapshot>(e.snapshot());
    }
    [[nodiscard]] std::shared_ptr<const terminal::ScreenSnapshot> snapshot() const override { return snap_; }
    void send(host::InputEvent /*input*/) override {}
    void resize(int /*rows*/, int /*cols*/) override {}
    [[nodiscard]] bool can_input() const override { return true; }

private:
    std::shared_ptr<const terminal::ScreenSnapshot> snap_;
};

class TerminalViewDrag : public testing::Test {
protected:
    void SetUp() override {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(800, 400);
        io.DeltaTime = 1.0f / 60.0f;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        fonts_.mono = io.Fonts->AddFontDefault();
        view_.theme.selection = kSelection;
        view_.copy_on_select = false;
        frame();
        frame();
    }
    void TearDown() override { ImGui::DestroyContext(); }

    void frame() {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(800, 400));
        ImGui::Begin("t", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
        origin_ = ImGui::GetCursorScreenPos();
        ImGui::PushFont(fonts_.mono, fonts_.mono_size);
        cell_w_ =
            std::max(1.0f, std::round(ImGui::GetFontBaked()->GetCharAdvance(static_cast<ImWchar>('M'))));
        ImGui::PopFont();
        view_.draw("##term", source_, fonts_, false);
        ImGui::End();
        ImGui::Render();
        for (ImTextureData* tex : *ImGui::GetDrawData()->Textures) {
            if (tex->Status == ImTextureStatus_WantCreate)
                tex->SetTexID(static_cast<ImTextureID>(1));
            if (tex->Status != ImTextureStatus_Destroyed)
                tex->SetStatus(tex->Status == ImTextureStatus_WantDestroy ? ImTextureStatus_Destroyed
                                                                          : ImTextureStatus_OK);
        }
    }
    void move_to(float col) {
        ImGui::GetIO().AddMousePosEvent(origin_.x + col * cell_w_, origin_.y + 4);
        frame();
    }
    // Drags from inside one cell of the first row to inside another; the mouse stays pressed.
    void drag(int from, int to) {
        move_to(static_cast<float>(from) + 0.5f);
        ImGui::GetIO().AddMouseButtonEvent(0, true);
        frame();
        move_to(static_cast<float>(to) + 0.5f);
        frame();
    }
    void release() {
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        frame();
        for (int i = 0; i < 60; ++i)  // later than a double click
            frame();
    }
    // The painted selection as [first, end) columns; {-1, -1} when there is none.
    [[nodiscard]] std::pair<int, int> painted() const {
        float x0 = FLT_MAX;
        float x1 = -FLT_MAX;
        for (const ImDrawList* dl : ImGui::GetDrawData()->CmdLists)
            for (const ImDrawVert& v : dl->VtxBuffer)
                if (v.col == kSelection) {
                    x0 = std::min(x0, v.pos.x);
                    x1 = std::max(x1, v.pos.x);
                }
        if (x1 < x0)
            return {-1, -1};
        return {static_cast<int>(std::lround((x0 - origin_.x) / cell_w_)),
                static_cast<int>(std::lround((x1 - origin_.x) / cell_w_))};
    }

    // columns: a=0, two wide characters=1..2 and 3..4, b=5
    FixedScreen source_{
        "a\xE4\xB8\xAD\xE6\x96\x87"
        "b"};
    ui::TerminalView view_;
    ui::FontSet fonts_;
    ImVec2 origin_;
    float cell_w_ = 1;
};

}  // namespace

TEST_F(TerminalViewDrag, WideCharactersArePaintedWholeWhicheverHalfTheDragEndsOn) {
    drag(2, 3);  // right half of the first to left half of the second
    EXPECT_EQ(painted(), (std::pair{1, 5}));
    release();
    drag(3, 2);  // the same, to the left
    EXPECT_EQ(painted(), (std::pair{1, 5}));
    release();
    drag(0, 1);
    EXPECT_EQ(painted(), (std::pair{0, 3}));
    release();
    drag(4, 5);
    EXPECT_EQ(painted(), (std::pair{3, 6}));
}

TEST_F(TerminalViewDrag, NarrowCharactersArePaintedCellByCell) {
    drag(5, 6);
    EXPECT_EQ(painted(), (std::pair{5, 7}));
}
