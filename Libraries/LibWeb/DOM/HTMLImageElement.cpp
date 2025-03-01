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

#include <LibCore/Timer.h>
#include <LibGfx/Bitmap.h>
#include <LibGfx/ImageDecoder.h>
#include <LibWeb/CSS/StyleResolver.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/DOM/Event.h>
#include <LibWeb/DOM/HTMLImageElement.h>
#include <LibWeb/Layout/LayoutImage.h>
#include <LibWeb/Loader/ResourceLoader.h>

namespace Web {

HTMLImageElement::HTMLImageElement(Document& document, const FlyString& tag_name)
    : HTMLElement(document, tag_name)
    , m_timer(Core::Timer::construct())
{
}

HTMLImageElement::~HTMLImageElement()
{
}

void HTMLImageElement::parse_attribute(const FlyString& name, const String& value)
{
    HTMLElement::parse_attribute(name, value);
    if (name.equals_ignoring_case("src"))
        load_image(value);
}

void HTMLImageElement::load_image(const String& src)
{
    LoadRequest request;
    request.set_url(document().complete_url(src));
    set_resource(ResourceLoader::the().load_resource(Resource::Type::Image, request));
}

void HTMLImageElement::resource_did_load()
{
    ASSERT(resource());

    if (!resource()->has_encoded_data()) {
        dbg() << "HTMLImageElement: Resource did load, but encoded data empty: " << this->src();
        return;
    }

    dbg() << "HTMLImageElement: Resource did load, encoded data looks tasty: " << this->src();

    m_image_decoder = resource()->ensure_decoder();

    if (m_image_decoder->is_animated() && m_image_decoder->frame_count() > 1) {
        const auto& first_frame = m_image_decoder->frame(0);
        m_timer->set_interval(first_frame.duration);
        m_timer->on_timeout = [this] { animate(); };
        m_timer->start();
    }

    document().update_layout();
    dispatch_event(Event::create("load"));
}

void HTMLImageElement::resource_did_fail()
{
    dbg() << "HTMLImageElement: Resource did fail: " << this->src();
    m_image_decoder = nullptr;
    document().update_layout();
    dispatch_event(Event::create("error"));
}

void HTMLImageElement::resource_did_replace_decoder()
{
    m_image_decoder = resource()->ensure_decoder();
}

void HTMLImageElement::animate()
{
    if (!layout_node()) {
        return;
    }

    m_current_frame_index = (m_current_frame_index + 1) % m_image_decoder->frame_count();
    const auto& current_frame = m_image_decoder->frame(m_current_frame_index);

    if (current_frame.duration != m_timer->interval()) {
        m_timer->restart(current_frame.duration);
    }

    if (m_current_frame_index == m_image_decoder->frame_count() - 1) {
        ++m_loops_completed;
        if (m_loops_completed > 0 && m_loops_completed == m_image_decoder->loop_count()) {
            m_timer->stop();
        }
    }

    layout_node()->set_needs_display();
}

int HTMLImageElement::preferred_width() const
{
    bool ok = false;
    int width = attribute(HTML::AttributeNames::width).to_int(ok);
    if (ok)
        return width;

    if (m_image_decoder)
        return m_image_decoder->width();

    return 0;
}

int HTMLImageElement::preferred_height() const
{
    bool ok = false;
    int height = attribute(HTML::AttributeNames::height).to_int(ok);
    if (ok)
        return height;

    if (m_image_decoder)
        return m_image_decoder->height();

    return 0;
}

RefPtr<LayoutNode> HTMLImageElement::create_layout_node(const StyleProperties* parent_style) const
{
    auto style = document().style_resolver().resolve_style(*this, parent_style);
    auto display = style->string_or_fallback(CSS::PropertyID::Display, "inline");
    if (display == "none")
        return nullptr;
    return adopt(*new LayoutImage(*this, move(style)));
}

const Gfx::Bitmap* HTMLImageElement::bitmap() const
{
    if (!m_image_decoder)
        return nullptr;

    if (m_image_decoder->is_animated()) {
        return m_image_decoder->frame(m_current_frame_index).image;
    }

    return m_image_decoder->bitmap();
}

void HTMLImageElement::set_visible_in_viewport(Badge<LayoutDocument>, bool visible_in_viewport)
{
    if (m_visible_in_viewport == visible_in_viewport)
        return;
    m_visible_in_viewport = visible_in_viewport;

    // FIXME: Don't update volatility every time. If we're here, we're probably scanning through
    //        the whole document, updating "is visible in viewport" flags, and this could lead
    //        to the same bitmap being marked volatile back and forth unnecessarily.
    if (resource())
        resource()->update_volatility();
}

}
