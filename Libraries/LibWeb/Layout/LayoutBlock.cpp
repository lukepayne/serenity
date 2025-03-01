/*
 * Copyright (c) 2018-2020, Andreas Kling <kling@serenityos.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <LibGUI/Painter.h>
#include <LibWeb/CSS/StyleResolver.h>
#include <LibWeb/DOM/Element.h>
#include <LibWeb/Layout/LayoutBlock.h>
#include <LibWeb/Layout/LayoutInline.h>
#include <LibWeb/Layout/LayoutReplaced.h>
#include <LibWeb/Layout/LayoutText.h>
#include <LibWeb/Layout/LayoutWidget.h>
#include <math.h>

namespace Web {

LayoutBlock::LayoutBlock(const Node* node, NonnullRefPtr<StyleProperties> style)
    : LayoutBox(node, move(style))
{
}

LayoutBlock::~LayoutBlock()
{
}

LayoutNode& LayoutBlock::inline_wrapper()
{
    if (!last_child() || !last_child()->is_block() || last_child()->node() != nullptr) {
        append_child(adopt(*new LayoutBlock(nullptr, style_for_anonymous_block())));
        last_child()->set_children_are_inline(true);
    }
    return *last_child();
}

void LayoutBlock::layout(LayoutMode layout_mode)
{
    compute_width();

    if (!is_inline())
        compute_position();

    layout_children(layout_mode);

    compute_height();
}

void LayoutBlock::layout_children(LayoutMode layout_mode)
{
    if (children_are_inline())
        layout_inline_children(layout_mode);
    else
        layout_block_children(layout_mode);
}

void LayoutBlock::layout_block_children(LayoutMode layout_mode)
{
    ASSERT(!children_are_inline());
    float content_height = 0;
    for_each_child([&](auto& child) {
        // FIXME: What should we do here? Something like a <table> might have a bunch of useless text children..
        if (child.is_inline())
            return;
        auto& child_block = static_cast<LayoutBlock&>(child);
        child_block.layout(layout_mode);

        if (!child_block.is_absolutely_positioned())
            content_height = child_block.rect().bottom() + child_block.box_model().full_margin(*this).bottom - rect().top();
    });
    if (layout_mode != LayoutMode::Default) {
        float max_width = 0;
        for_each_child([&](auto& child) {
            if (child.is_box() && !child.is_absolutely_positioned())
                max_width = max(max_width, to<LayoutBox>(child).width());
        });
        rect().set_width(max_width);
    }
    rect().set_height(content_height);
}

void LayoutBlock::layout_inline_children(LayoutMode layout_mode)
{
    ASSERT(children_are_inline());
    m_line_boxes.clear();
    for_each_child([&](auto& child) {
        ASSERT(child.is_inline());
        child.split_into_lines(*this, layout_mode);
    });

    for (auto& line_box : m_line_boxes) {
        line_box.trim_trailing_whitespace();
    }

    float min_line_height = style().line_height(*this);
    float line_spacing = min_line_height - style().font().glyph_height();
    float content_height = 0;

    // FIXME: This should be done by the CSS parser!
    CSS::ValueID text_align = CSS::ValueID::Left;
    auto text_align_string = style().string_or_fallback(CSS::PropertyID::TextAlign, "left");
    if (text_align_string == "center")
        text_align = CSS::ValueID::Center;
    else if (text_align_string == "left")
        text_align = CSS::ValueID::Left;
    else if (text_align_string == "right")
        text_align = CSS::ValueID::Right;
    else if (text_align_string == "justify")
        text_align = CSS::ValueID::Justify;

    float max_linebox_width = 0;

    for (auto& line_box : m_line_boxes) {
        float max_height = min_line_height;
        for (auto& fragment : line_box.fragments()) {
            max_height = max(max_height, fragment.rect().height());
        }

        float x_offset = x();
        float excess_horizontal_space = (float)width() - line_box.width();

        switch (text_align) {
        case CSS::ValueID::Center:
            x_offset += excess_horizontal_space / 2;
            break;
        case CSS::ValueID::Right:
            x_offset += excess_horizontal_space;
            break;
        case CSS::ValueID::Left:
        case CSS::ValueID::Justify:
        default:
            break;
        }

        float excess_horizontal_space_including_whitespace = excess_horizontal_space;
        int whitespace_count = 0;
        if (text_align == CSS::ValueID::Justify) {
            for (auto& fragment : line_box.fragments()) {
                if (fragment.is_justifiable_whitespace()) {
                    ++whitespace_count;
                    excess_horizontal_space_including_whitespace += fragment.rect().width();
                }
            }
        }

        float justified_space_width = whitespace_count ? (excess_horizontal_space_including_whitespace / (float)whitespace_count) : 0;

        for (size_t i = 0; i < line_box.fragments().size(); ++i) {
            auto& fragment = line_box.fragments()[i];

            if (fragment.layout_node().is_absolutely_positioned())
                continue;

            // Vertically align everyone's bottom to the line.
            // FIXME: Support other kinds of vertical alignment.
            fragment.rect().set_x(roundf(x_offset + fragment.rect().x()));
            fragment.rect().set_y(y() + content_height + (max_height - fragment.rect().height()) - (line_spacing / 2));

            if (text_align == CSS::ValueID::Justify) {
                if (fragment.is_justifiable_whitespace()) {
                    if (fragment.rect().width() != justified_space_width) {
                        float diff = justified_space_width - fragment.rect().width();
                        fragment.rect().set_width(justified_space_width);
                        // Shift subsequent sibling fragments to the right to adjust for change in width.
                        for (size_t j = i + 1; j < line_box.fragments().size(); ++j) {
                            line_box.fragments()[j].rect().move_by(diff, 0);
                        }
                    }
                }
            }

            if (is<LayoutReplaced>(fragment.layout_node()))
                const_cast<LayoutReplaced&>(to<LayoutReplaced>(fragment.layout_node())).set_rect(fragment.rect());

            if (fragment.layout_node().is_inline_block()) {
                auto& inline_block = const_cast<LayoutBlock&>(to<LayoutBlock>(fragment.layout_node()));
                inline_block.set_rect(fragment.rect());
                inline_block.layout(layout_mode);
            }

            float final_line_box_width = 0;
            for (auto& fragment : line_box.fragments())
                final_line_box_width += fragment.rect().width();
            line_box.m_width = final_line_box_width;

            max_linebox_width = max(max_linebox_width, final_line_box_width);
        }

        content_height += max_height;
    }

    if (layout_mode != LayoutMode::Default) {
        rect().set_width(max_linebox_width);
    }

    rect().set_height(content_height);
}

void LayoutBlock::compute_width()
{
    auto& style = this->style();

    auto auto_value = Length();
    auto zero_value = Length(0, Length::Type::Px);

    Length margin_left;
    Length margin_right;
    Length border_left;
    Length border_right;
    Length padding_left;
    Length padding_right;

    auto& containing_block = *this->containing_block();

    auto try_compute_width = [&](const auto& a_width) {
        Length width = a_width;
#ifdef HTML_DEBUG
        dbg() << " Left: " << margin_left << "+" << border_left << "+" << padding_left;
        dbg() << "Right: " << margin_right << "+" << border_right << "+" << padding_right;
#endif
        margin_left = style.length_or_fallback(CSS::PropertyID::MarginLeft, zero_value, containing_block.width());
        margin_right = style.length_or_fallback(CSS::PropertyID::MarginRight, zero_value, containing_block.width());
        border_left = style.length_or_fallback(CSS::PropertyID::BorderLeftWidth, zero_value);
        border_right = style.length_or_fallback(CSS::PropertyID::BorderRightWidth, zero_value);
        padding_left = style.length_or_fallback(CSS::PropertyID::PaddingLeft, zero_value, containing_block.width());
        padding_right = style.length_or_fallback(CSS::PropertyID::PaddingRight, zero_value, containing_block.width());

        float total_px = 0;
        for (auto& value : { margin_left, border_left, padding_left, width, padding_right, border_right, margin_right }) {
            total_px += value.to_px(*this);
        }

#ifdef HTML_DEBUG
        dbg() << "Total: " << total_px;
#endif

        if (!is_replaced() && !is_inline()) {
            // 10.3.3 Block-level, non-replaced elements in normal flow
            // If 'width' is not 'auto' and 'border-left-width' + 'padding-left' + 'width' + 'padding-right' + 'border-right-width' (plus any of 'margin-left' or 'margin-right' that are not 'auto') is larger than the width of the containing block, then any 'auto' values for 'margin-left' or 'margin-right' are, for the following rules, treated as zero.
            if (width.is_auto() && total_px > containing_block.width()) {
                if (margin_left.is_auto())
                    margin_left = zero_value;
                if (margin_right.is_auto())
                    margin_right = zero_value;
            }

            // 10.3.3 cont'd.
            auto underflow_px = containing_block.width() - total_px;

            if (width.is_auto()) {
                if (margin_left.is_auto())
                    margin_left = zero_value;
                if (margin_right.is_auto())
                    margin_right = zero_value;
                if (underflow_px >= 0) {
                    width = Length(underflow_px, Length::Type::Px);
                } else {
                    width = zero_value;
                    margin_right = Length(margin_right.to_px(*this) + underflow_px, Length::Type::Px);
                }
            } else {
                if (!margin_left.is_auto() && !margin_right.is_auto()) {
                    margin_right = Length(margin_right.to_px(*this) + underflow_px, Length::Type::Px);
                } else if (!margin_left.is_auto() && margin_right.is_auto()) {
                    margin_right = Length(underflow_px, Length::Type::Px);
                } else if (margin_left.is_auto() && !margin_right.is_auto()) {
                    margin_left = Length(underflow_px, Length::Type::Px);
                } else { // margin_left.is_auto() && margin_right.is_auto()
                    auto half_of_the_underflow = Length(underflow_px / 2, Length::Type::Px);
                    margin_left = half_of_the_underflow;
                    margin_right = half_of_the_underflow;
                }
            }
        } else if (!is_replaced() && is_inline_block()) {

            // 10.3.9 'Inline-block', non-replaced elements in normal flow

            // A computed value of 'auto' for 'margin-left' or 'margin-right' becomes a used value of '0'.
            if (margin_left.is_auto())
                margin_left = zero_value;
            if (margin_right.is_auto())
                margin_right = zero_value;

            // If 'width' is 'auto', the used value is the shrink-to-fit width as for floating elements.
            if (width.is_auto()) {
                auto greatest_child_width = [&] {
                    float max_width = 0;
                    if (children_are_inline()) {
                        for (auto& box : line_boxes()) {
                            max_width = max(max_width, box.width());
                        }
                    } else {
                        for_each_child([&](auto& child) {
                            if (child.is_box())
                                max_width = max(max_width, to<LayoutBox>(child).width());
                        });
                    }
                    return max_width;
                };

                // Find the available width: in this case, this is the width of the containing
                // block minus the used values of 'margin-left', 'border-left-width', 'padding-left',
                // 'padding-right', 'border-right-width', 'margin-right', and the widths of any relevant scroll bars.

                float available_width = containing_block.width()
                    - margin_left.to_px(*this) - border_left.to_px(*this) - padding_left.to_px(*this)
                    - padding_right.to_px(*this) - border_right.to_px(*this) - margin_right.to_px(*this);

                // Calculate the preferred width by formatting the content without breaking lines
                // other than where explicit line breaks occur.
                layout_children(LayoutMode::OnlyRequiredLineBreaks);
                float preferred_width = greatest_child_width();

                // Also calculate the preferred minimum width, e.g., by trying all possible line breaks.
                // CSS 2.2 does not define the exact algorithm.

                layout_children(LayoutMode::AllPossibleLineBreaks);
                float preferred_minimum_width = greatest_child_width();

                // Then the shrink-to-fit width is: min(max(preferred minimum width, available width), preferred width).
                width = Length(min(max(preferred_minimum_width, available_width), preferred_width), Length::Type::Px);
            }
        }

        return width;
    };

    auto specified_width = style.length_or_fallback(CSS::PropertyID::Width, auto_value, containing_block.width());

    // 1. The tentative used width is calculated (without 'min-width' and 'max-width')
    auto used_width = try_compute_width(specified_width);

    // 2. The tentative used width is greater than 'max-width', the rules above are applied again,
    //    but this time using the computed value of 'max-width' as the computed value for 'width'.
    auto specified_max_width = style.length_or_fallback(CSS::PropertyID::MaxWidth, auto_value, containing_block.width());
    if (!specified_max_width.is_auto()) {
        if (used_width.to_px(*this) > specified_max_width.to_px(*this)) {
            used_width = try_compute_width(specified_max_width);
        }
    }

    // 3. If the resulting width is smaller than 'min-width', the rules above are applied again,
    //    but this time using the value of 'min-width' as the computed value for 'width'.
    auto specified_min_width = style.length_or_fallback(CSS::PropertyID::MinWidth, auto_value, containing_block.width());
    if (!specified_min_width.is_auto()) {
        if (used_width.to_px(*this) < specified_min_width.to_px(*this)) {
            used_width = try_compute_width(specified_min_width);
        }
    }

    rect().set_width(used_width.to_px(*this));
    box_model().margin().left = margin_left;
    box_model().margin().right = margin_right;
    box_model().border().left = border_left;
    box_model().border().right = border_right;
    box_model().padding().left = padding_left;
    box_model().padding().right = padding_right;
}

void LayoutBlock::compute_position()
{
    auto& style = this->style();

    auto zero_value = Length(0, Length::Type::Px);

    auto& containing_block = *this->containing_block();

    if (style.position() == CSS::Position::Absolute) {
        box_model().offset().top = style.length_or_fallback(CSS::PropertyID::Top, zero_value, containing_block.height());
        box_model().offset().right = style.length_or_fallback(CSS::PropertyID::Right, zero_value, containing_block.width());
        box_model().offset().bottom = style.length_or_fallback(CSS::PropertyID::Bottom, zero_value, containing_block.height());
        box_model().offset().left = style.length_or_fallback(CSS::PropertyID::Left, zero_value, containing_block.width());
    }

    box_model().margin().top = style.length_or_fallback(CSS::PropertyID::MarginTop, zero_value, containing_block.width());
    box_model().margin().bottom = style.length_or_fallback(CSS::PropertyID::MarginBottom, zero_value, containing_block.width());
    box_model().border().top = style.length_or_fallback(CSS::PropertyID::BorderTopWidth, zero_value);
    box_model().border().bottom = style.length_or_fallback(CSS::PropertyID::BorderBottomWidth, zero_value);
    box_model().padding().top = style.length_or_fallback(CSS::PropertyID::PaddingTop, zero_value, containing_block.width());
    box_model().padding().bottom = style.length_or_fallback(CSS::PropertyID::PaddingBottom, zero_value, containing_block.width());

    float position_x = box_model().margin().left.to_px(*this)
        + box_model().border().left.to_px(*this)
        + box_model().padding().left.to_px(*this)
        + box_model().offset().left.to_px(*this);

    if (style.position() != CSS::Position::Absolute || containing_block.style().position() == CSS::Position::Absolute)
        position_x += containing_block.x();

    rect().set_x(position_x);

    float position_y = box_model().full_margin(*this).top
        + box_model().offset().top.to_px(*this);

    if (style.position() != CSS::Position::Absolute || containing_block.style().position() == CSS::Position::Absolute) {
        LayoutBlock* relevant_sibling = previous_sibling();
        while (relevant_sibling != nullptr) {
            if (relevant_sibling->style().position() != CSS::Position::Absolute)
                break;
            relevant_sibling = relevant_sibling->previous_sibling();
        }

        if (relevant_sibling == nullptr) {
            position_y += containing_block.y();
        } else {
            auto& previous_sibling_rect = relevant_sibling->rect();
            auto& previous_sibling_style = relevant_sibling->box_model();
            position_y += previous_sibling_rect.y() + previous_sibling_rect.height();
            position_y += previous_sibling_style.full_margin(*this).bottom;
        }
    }

    rect().set_y(position_y);
}

void LayoutBlock::compute_height()
{
    auto& style = this->style();
    auto height = style.length_or_fallback(CSS::PropertyID::Height, Length(), containing_block()->height());
    if (height.is_absolute())
        rect().set_height(height.to_px(*this));
}

void LayoutBlock::render(RenderingContext& context)
{
    if (!is_visible())
        return;

    LayoutBox::render(context);

    if (children_are_inline()) {
        for (auto& line_box : m_line_boxes) {
            for (auto& fragment : line_box.fragments()) {
                if (context.should_show_line_box_borders())
                    context.painter().draw_rect(enclosing_int_rect(fragment.rect()), Color::Green);
                fragment.render(context);
            }
        }
    }
}

HitTestResult LayoutBlock::hit_test(const Gfx::Point& position) const
{
    if (!children_are_inline())
        return LayoutBox::hit_test(position);

    HitTestResult result;
    for (auto& line_box : m_line_boxes) {
        for (auto& fragment : line_box.fragments()) {
            if (enclosing_int_rect(fragment.rect()).contains(position)) {
                if (fragment.layout_node().is_block())
                    return to<LayoutBlock>(fragment.layout_node()).hit_test(position);
                return { fragment.layout_node(), fragment.text_index_at(position.x()) };
            }
        }
    }

    // FIXME: This should be smarter about the text position if we're hitting a block
    //        that has text inside it, but `position` is to the right of the text box.
    return { rect().contains(position.x(), position.y()) ? this : nullptr };
}

NonnullRefPtr<StyleProperties> LayoutBlock::style_for_anonymous_block() const
{
    auto new_style = StyleProperties::create();

    style().for_each_property([&](auto property_id, auto& value) {
        if (StyleResolver::is_inherited_property(property_id))
            new_style->set_property(property_id, value);
    });

    return new_style;
}

LineBox& LayoutBlock::ensure_last_line_box()
{
    if (m_line_boxes.is_empty())
        m_line_boxes.append(LineBox());
    return m_line_boxes.last();
}

LineBox& LayoutBlock::add_line_box()
{
    m_line_boxes.append(LineBox());
    return m_line_boxes.last();
}

void LayoutBlock::split_into_lines(LayoutBlock& container, LayoutMode layout_mode)
{
    ASSERT(is_inline());

    layout(layout_mode);

    auto* line_box = &container.ensure_last_line_box();
    if (line_box->width() > 0 && line_box->width() + width() > container.width())
        line_box = &container.add_line_box();
    line_box->add_fragment(*this, 0, 0, width(), height());
}

}
