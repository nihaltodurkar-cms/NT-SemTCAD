// GroupBox (N3a, NATIVE-DESKTOP-PLAN.md 27.8.1): a titled frame around a layout, QGroupBox's measured subset (10
// constructions: a title, a layout; none checkable, no '&' in a title). The frame and the title band are contents
// margins (Widget::setContentsMargins), so the layout set on it with setLayout<...>() -- with its usual 9-DIP margins --
// sits inside the frame, as in Qt. The title is drawn over the frame's top edge.
#pragma once

#include "ui/core/widget.hpp"

#include <string>

namespace tcad::ui {

class GroupBox : public Widget {
public:
    explicit GroupBox(std::string title = {});

    void setTitle(std::string title);
    const std::string& title() const { return title_; }

    SizeF sizeHint() const override;         // the layout's, and at least wide enough for the title
    SizeF minimumSizeHint() const override;
    void paint(Painter& p) override;
    Role accessibleRole() const override { return Role::Group; }
    Margins contentsMargins() const override;  // the frame, and the title band on top

private:
    float titleHeight() const;
    float titleWidth() const;

    std::string title_;
};

}  // namespace tcad::ui
