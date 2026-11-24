#pragma once
// The rules behind the Review layout (terminal on the left, the agent's uncommitted changes and
// their diff on the right), kept apart from its drawing (apps/desktop/review_layout.cpp) so they
// can be unit-tested.
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace agentdesk::ui::review {

// The changed file `step` places after `current` in `paths` (step -1: before), wrapping around at
// either end. A `current` not in the list gives the first file (step > 0) or the last. Empty list:
// "".
std::string next_change(const std::vector<std::string>& paths, const std::string& current, int step);

// "src/auth/token.ts" -> {"src/auth/", "token.ts"}; no folder -> {"", path}.
struct PathParts {
    std::string dir;
    std::string name;
};
PathParts split_path(const std::string& path);

// Columns a UTF-8 text takes in a fixed grid: East Asian wide characters count 2, the rest 1.
int display_width(std::string_view text);

// `text` cut in the middle with "…" so that `measure(result) <= max` (the start and the end stay,
// which keep the folder root and the file name). Whole UTF-8 characters only. Returns the text
// itself when it fits, "…" alone when even one character on each side does not.
std::string middle_ellipsis(const std::string& text, float max,
                            const std::function<float(std::string_view)>& measure);
// The same in grid columns (display_width).
std::string middle_ellipsis(const std::string& text, int max_cols);

// Where the list of changed files and the diff go in the right column (`w` x `h`, real px;
// `scale` turns design px into real px). Wide enough (760 design px and up): the list sits left of
// the diff, 260 wide. Otherwise the list sits on top: 35 % of the height, 96 to 300 high.
// `list_size` (real px, > 0) is the person's drag of the 6 px splitter between them and wins
// within the same bounds (side by side: 180 to half the width).
struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
};
struct Split {
    bool side_by_side = false;
    Rect list;
    Rect splitter;  // the 6 px drag handle between list and diff
    Rect diff;
};
Split review_split(float w, float h, float scale, float list_size = 0.0f);

}  // namespace agentdesk::ui::review
