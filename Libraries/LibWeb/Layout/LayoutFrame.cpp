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
#include <LibGUI/ScrollBar.h>
#include <LibGUI/Widget.h>
#include <LibGfx/Font.h>
#include <LibGfx/StylePainter.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/Frame/Frame.h>
#include <LibWeb/Layout/LayoutDocument.h>
#include <LibWeb/Layout/LayoutFrame.h>
#include <LibWeb/PageView.h>

namespace Web {

LayoutFrame::LayoutFrame(const Element& element, NonnullRefPtr<StyleProperties> style)
    : LayoutReplaced(element, move(style))
{
}

LayoutFrame::~LayoutFrame()
{
}

void LayoutFrame::layout(LayoutMode layout_mode)
{
    ASSERT(node().hosted_frame());

    set_has_intrinsic_width(true);
    set_has_intrinsic_height(true);
    // FIXME: Do proper error checking, etc.
    bool ok;
    set_intrinsic_width(node().attribute(HTML::AttributeNames::width).to_int(ok));
    set_intrinsic_height(node().attribute(HTML::AttributeNames::height).to_int(ok));

    LayoutReplaced::layout(layout_mode);
}

void LayoutFrame::render(RenderingContext& context)
{
    LayoutReplaced::render(context);

    context.painter().save();
    auto old_viewport_rect = context.viewport_rect();

    context.painter().add_clip_rect(enclosing_int_rect(rect()));
    context.painter().translate(x(), y());

    context.set_viewport_rect({ {}, node().hosted_frame()->size() });
    node().hosted_frame()->document()->layout_node()->render(context);

    context.set_viewport_rect(old_viewport_rect);
    context.painter().restore();
}

void LayoutFrame::did_set_rect()
{
    LayoutReplaced::did_set_rect();

    ASSERT(node().hosted_frame());
    node().hosted_frame()->set_size(Gfx::Size(rect().width(), rect().height()));
}

}
