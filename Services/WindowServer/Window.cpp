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

#include "Window.h"
#include "AppletManager.h"
#include "ClientConnection.h"
#include "Compositor.h"
#include "Event.h"
#include "EventLoop.h"
#include "Screen.h"
#include "WindowManager.h"
#include <AK/Badge.h>
#include <WindowServer/WindowClientEndpoint.h>

namespace WindowServer {

static String default_window_icon_path()
{
    return "/res/icons/16x16/window.png";
}

static Gfx::Bitmap& default_window_icon()
{
    static Gfx::Bitmap* s_icon;
    if (!s_icon)
        s_icon = Gfx::Bitmap::load_from_file(default_window_icon_path()).leak_ref();
    return *s_icon;
}

static Gfx::Bitmap& minimize_icon()
{
    static Gfx::Bitmap* s_icon;
    if (!s_icon)
        s_icon = Gfx::Bitmap::load_from_file("/res/icons/16x16/window-minimize.png").leak_ref();
    return *s_icon;
}

static Gfx::Bitmap& maximize_icon()
{
    static Gfx::Bitmap* s_icon;
    if (!s_icon)
        s_icon = Gfx::Bitmap::load_from_file("/res/icons/16x16/window-maximize.png").leak_ref();
    return *s_icon;
}

static Gfx::Bitmap& restore_icon()
{
    static Gfx::Bitmap* s_icon;
    if (!s_icon)
        s_icon = Gfx::Bitmap::load_from_file("/res/icons/16x16/window-restore.png").leak_ref();
    return *s_icon;
}

static Gfx::Bitmap& close_icon()
{
    static Gfx::Bitmap* s_icon;
    if (!s_icon)
        s_icon = Gfx::Bitmap::load_from_file("/res/icons/16x16/window-close.png").leak_ref();
    return *s_icon;
}

Window::Window(Core::Object& parent, WindowType type)
    : Core::Object(&parent)
    , m_type(type)
    , m_icon(default_window_icon())
    , m_frame(*this)
{
    WindowManager::the().add_window(*this);
}

Window::Window(ClientConnection& client, WindowType window_type, int window_id, bool modal, bool minimizable, bool frameless, bool resizable, bool fullscreen)
    : Core::Object(&client)
    , m_client(&client)
    , m_type(window_type)
    , m_modal(modal)
    , m_minimizable(minimizable)
    , m_frameless(frameless)
    , m_resizable(resizable)
    , m_fullscreen(fullscreen)
    , m_window_id(window_id)
    , m_client_id(client.client_id())
    , m_icon(default_window_icon())
    , m_frame(*this)
{
    // FIXME: This should not be hard-coded here.
    if (m_type == WindowType::Taskbar) {
        m_wm_event_mask = WMEventMask::WindowStateChanges | WMEventMask::WindowRemovals | WMEventMask::WindowIconChanges;
        m_listens_to_wm_events = true;
    }

    WindowManager::the().add_window(*this);
}

Window::~Window()
{
    // Detach from client at the start of teardown since we don't want
    // to confuse things by trying to send messages to it.
    m_client = nullptr;

    WindowManager::the().remove_window(*this);
}

void Window::set_title(const String& title)
{
    if (m_title == title)
        return;
    m_title = title;
    WindowManager::the().notify_title_changed(*this);
}

void Window::set_rect(const Gfx::Rect& rect)
{
    ASSERT(!rect.is_empty());
    Gfx::Rect old_rect;
    if (m_rect == rect)
        return;
    old_rect = m_rect;
    m_rect = rect;
    if (!m_client && (!m_backing_store || old_rect.size() != rect.size())) {
        m_backing_store = Gfx::Bitmap::create(Gfx::BitmapFormat::RGB32, m_rect.size());
    }
    m_frame.notify_window_rect_changed(old_rect, rect);
}

void Window::set_rect_without_repaint(const Gfx::Rect& rect)
{
    ASSERT(!rect.is_empty());
    if (m_rect == rect)
        return;
    auto old_rect = m_rect;
    m_rect = rect;

    if (old_rect.size() == m_rect.size()) {
        auto delta = m_rect.location() - old_rect.location();
        for (auto& child_window : m_child_windows) {
            if (child_window)
                child_window->move_by(delta);
        }
    }

    m_frame.notify_window_rect_changed(old_rect, rect);
}

void Window::handle_mouse_event(const MouseEvent& event)
{
    set_automatic_cursor_tracking_enabled(event.buttons() != 0);

    switch (event.type()) {
    case Event::MouseMove:
        m_client->post_message(Messages::WindowClient::MouseMove(m_window_id, event.position(), (u32)event.button(), event.buttons(), event.modifiers(), event.wheel_delta(), event.is_drag(), event.drag_data_type()));
        break;
    case Event::MouseDown:
        m_client->post_message(Messages::WindowClient::MouseDown(m_window_id, event.position(), (u32)event.button(), event.buttons(), event.modifiers(), event.wheel_delta()));
        break;
    case Event::MouseDoubleClick:
        m_client->post_message(Messages::WindowClient::MouseDoubleClick(m_window_id, event.position(), (u32)event.button(), event.buttons(), event.modifiers(), event.wheel_delta()));
        break;
    case Event::MouseUp:
        m_client->post_message(Messages::WindowClient::MouseUp(m_window_id, event.position(), (u32)event.button(), event.buttons(), event.modifiers(), event.wheel_delta()));
        break;
    case Event::MouseWheel:
        m_client->post_message(Messages::WindowClient::MouseWheel(m_window_id, event.position(), (u32)event.button(), event.buttons(), event.modifiers(), event.wheel_delta()));
        break;
    default:
        ASSERT_NOT_REACHED();
    }
}

void Window::update_menu_item_text(PopupMenuItem item)
{
    if (m_window_menu) {
        m_window_menu->item((int)item).set_text(item == PopupMenuItem::Minimize ? (m_minimized ? "Unminimize" : "Minimize") : (m_maximized ? "Restore" : "Maximize"));
        m_window_menu->redraw();
    }
}

void Window::update_menu_item_enabled(PopupMenuItem item)
{
    if (m_window_menu) {
        m_window_menu->item((int)item).set_enabled(item == PopupMenuItem::Minimize ? m_minimizable : m_resizable);
        m_window_menu->redraw();
    }
}

void Window::set_minimized(bool minimized)
{
    if (m_minimized == minimized)
        return;
    if (minimized && !m_minimizable)
        return;
    if (is_blocked_by_modal_window())
        return;
    m_minimized = minimized;
    update_menu_item_text(PopupMenuItem::Minimize);
    start_minimize_animation();
    if (!minimized)
        request_update({ {}, size() });
    invalidate();
    WindowManager::the().notify_minimization_state_changed(*this);
}

void Window::set_minimizable(bool minimizable)
{
    if (m_minimizable == minimizable)
        return;
    m_minimizable = minimizable;
    update_menu_item_enabled(PopupMenuItem::Minimize);
    // TODO: Hide/show (or alternatively change enabled state of) window minimize button dynamically depending on value of m_minimizable
}

void Window::set_opacity(float opacity)
{
    if (m_opacity == opacity)
        return;
    m_opacity = opacity;
    WindowManager::the().notify_opacity_changed(*this);
}

void Window::set_occluded(bool occluded)
{
    if (m_occluded == occluded)
        return;
    m_occluded = occluded;
    WindowManager::the().notify_occlusion_state_changed(*this);
}

void Window::set_maximized(bool maximized)
{
    if (m_maximized == maximized)
        return;
    if (maximized && !is_resizable())
        return;
    if (is_blocked_by_modal_window())
        return;
    set_tiled(WindowTileType::None);
    m_maximized = maximized;
    update_menu_item_text(PopupMenuItem::Maximize);
    auto old_rect = m_rect;
    if (maximized) {
        m_unmaximized_rect = m_rect;
        set_rect(WindowManager::the().maximized_window_rect(*this));
    } else {
        set_rect(m_unmaximized_rect);
    }
    m_frame.did_set_maximized({}, maximized);
    Core::EventLoop::current().post_event(*this, make<ResizeEvent>(old_rect, m_rect));
}

void Window::set_resizable(bool resizable)
{
    if (m_resizable == resizable)
        return;
    m_resizable = resizable;
    update_menu_item_enabled(PopupMenuItem::Maximize);
    // TODO: Hide/show (or alternatively change enabled state of) window maximize button dynamically depending on value of is_resizable()
}

void Window::event(Core::Event& event)
{
    if (!m_client) {
        ASSERT(parent());
        event.ignore();
        return;
    }

    if (is_blocked_by_modal_window())
        return;

    if (static_cast<Event&>(event).is_mouse_event())
        return handle_mouse_event(static_cast<const MouseEvent&>(event));

    switch (event.type()) {
    case Event::WindowEntered:
        m_client->post_message(Messages::WindowClient::WindowEntered(m_window_id));
        break;
    case Event::WindowLeft:
        m_client->post_message(Messages::WindowClient::WindowLeft(m_window_id));
        break;
    case Event::KeyDown:
        m_client->post_message(
            Messages::WindowClient::KeyDown(m_window_id,
                (u8) static_cast<const KeyEvent&>(event).character(),
                (u32) static_cast<const KeyEvent&>(event).key(),
                static_cast<const KeyEvent&>(event).modifiers(),
                (u32) static_cast<const KeyEvent&>(event).scancode()));
        break;
    case Event::KeyUp:
        m_client->post_message(
            Messages::WindowClient::KeyUp(m_window_id,
                (u8) static_cast<const KeyEvent&>(event).character(),
                (u32) static_cast<const KeyEvent&>(event).key(),
                static_cast<const KeyEvent&>(event).modifiers(),
                (u32) static_cast<const KeyEvent&>(event).scancode()));
        break;
    case Event::WindowActivated:
        m_client->post_message(Messages::WindowClient::WindowActivated(m_window_id));
        break;
    case Event::WindowDeactivated:
        m_client->post_message(Messages::WindowClient::WindowDeactivated(m_window_id));
        break;
    case Event::WindowCloseRequest:
        m_client->post_message(Messages::WindowClient::WindowCloseRequest(m_window_id));
        break;
    case Event::WindowResized:
        m_client->post_message(
            Messages::WindowClient::WindowResized(
                m_window_id,
                static_cast<const ResizeEvent&>(event).old_rect(),
                static_cast<const ResizeEvent&>(event).rect()));
        break;
    default:
        break;
    }
}

void Window::set_global_cursor_tracking_enabled(bool enabled)
{
    m_global_cursor_tracking_enabled = enabled;
}

void Window::set_visible(bool b)
{
    if (m_visible == b)
        return;
    m_visible = b;
    invalidate();
}

void Window::invalidate()
{
    Compositor::the().invalidate(frame().rect());
}

void Window::invalidate(const Gfx::Rect& rect)
{
    if (type() == WindowType::MenuApplet) {
        AppletManager::the().invalidate_applet(*this, rect);
        return;
    }

    if (rect.is_empty()) {
        invalidate();
        return;
    }
    auto outer_rect = frame().rect();
    auto inner_rect = rect;
    inner_rect.move_by(position());
    // FIXME: This seems slightly wrong; the inner rect shouldn't intersect the border part of the outer rect.
    inner_rect.intersect(outer_rect);
    Compositor::the().invalidate(inner_rect);
}

bool Window::is_active() const
{
    return WindowManager::the().active_window() == this;
}

bool Window::is_blocked_by_modal_window() const
{
    bool is_any_modal = false;
    const Window* next = this;
    while (!is_any_modal && next) {
        is_any_modal = next->is_modal();
        next = next->parent_window();
    }

    return !is_any_modal && client() && client()->is_showing_modal_window();
}

void Window::set_default_icon()
{
    m_icon = default_window_icon();
}

void Window::request_update(const Gfx::Rect& rect, bool ignore_occlusion)
{
    if (m_pending_paint_rects.is_empty()) {
        deferred_invoke([this, ignore_occlusion](auto&) {
            client()->post_paint_message(*this, ignore_occlusion);
        });
    }
    m_pending_paint_rects.add(rect);
}

void Window::popup_window_menu(const Gfx::Point& position)
{
    if (!m_window_menu) {
        m_window_menu = Menu::construct(nullptr, -1, "(Window Menu)");
        m_window_menu->set_window_menu_of(*this);

        auto minimize_item = make<MenuItem>(*m_window_menu, 1, m_minimized ? "Unminimize" : "Minimize");
        m_window_menu_minimize_item = minimize_item.ptr();
        m_window_menu->add_item(move(minimize_item));

        auto maximize_item = make<MenuItem>(*m_window_menu, 2, m_maximized ? "Restore" : "Maximize");
        m_window_menu_maximize_item = maximize_item.ptr();
        m_window_menu->add_item(move(maximize_item));

        m_window_menu->add_item(make<MenuItem>(*m_window_menu, MenuItem::Type::Separator));

        auto close_item = make<MenuItem>(*m_window_menu, 3, "Close");
        close_item->set_icon(&close_icon());
        m_window_menu->add_item(move(close_item));

        m_window_menu->item((int)PopupMenuItem::Minimize).set_enabled(m_minimizable);
        m_window_menu->item((int)PopupMenuItem::Maximize).set_enabled(m_resizable);

        m_window_menu->on_item_activation = [&](auto& item) {
            switch (item.identifier()) {
            case 1:
                set_minimized(!m_minimized);
                if (!m_minimized)
                    WindowManager::the().move_to_front_and_make_active(*this);
                break;
            case 2:
                set_maximized(!m_maximized);
                if (m_minimized)
                    set_minimized(false);
                WindowManager::the().move_to_front_and_make_active(*this);
                break;
            case 3:
                request_close();
                break;
            }
        };
    }
    m_window_menu_minimize_item->set_icon(m_minimized ? nullptr : &minimize_icon());
    m_window_menu_maximize_item->set_icon(m_maximized ? &restore_icon() : &maximize_icon());

    m_window_menu->popup(position);
}

void Window::request_close()
{
    Event close_request(Event::WindowCloseRequest);
    event(close_request);
}

void Window::set_fullscreen(bool fullscreen)
{
    if (m_fullscreen == fullscreen)
        return;
    m_fullscreen = fullscreen;
    Gfx::Rect new_window_rect = m_rect;
    if (m_fullscreen) {
        m_saved_nonfullscreen_rect = m_rect;
        new_window_rect = Screen::the().rect();
    } else if (!m_saved_nonfullscreen_rect.is_empty()) {
        new_window_rect = m_saved_nonfullscreen_rect;
    }
    Core::EventLoop::current().post_event(*this, make<ResizeEvent>(m_rect, new_window_rect));
    set_rect(new_window_rect);
}

Gfx::Rect Window::tiled_rect(WindowTileType tiled) const
{
    int frame_width = (m_frame.rect().width() - m_rect.width()) / 2;
    switch (tiled) {
    case WindowTileType::None:
        return m_untiled_rect;
    case WindowTileType::Left:
        return Gfx::Rect(0,
            WindowManager::the().maximized_window_rect(*this).y(),
            Screen::the().width() / 2 - frame_width,
            WindowManager::the().maximized_window_rect(*this).height());
    case WindowTileType::Right:
        return Gfx::Rect(Screen::the().width() / 2 + frame_width,
            WindowManager::the().maximized_window_rect(*this).y(),
            Screen::the().width() / 2 - frame_width,
            WindowManager::the().maximized_window_rect(*this).height());
    default:
        ASSERT_NOT_REACHED();
    }
}

void Window::set_tiled(WindowTileType tiled)
{
    if (m_tiled == tiled)
        return;

    m_tiled = tiled;
    auto old_rect = m_rect;
    if (tiled != WindowTileType::None)
        m_untiled_rect = m_rect;
    set_rect(tiled_rect(tiled));
    Core::EventLoop::current().post_event(*this, make<ResizeEvent>(old_rect, m_rect));
}

void Window::detach_client(Badge<ClientConnection>)
{
    m_client = nullptr;
}

void Window::recalculate_rect()
{
    if (!is_resizable())
        return;

    auto old_rect = m_rect;
    if (m_tiled != WindowTileType::None)
        set_rect(tiled_rect(m_tiled));
    else if (is_maximized())
        set_rect(WindowManager::the().maximized_window_rect(*this));
    Core::EventLoop::current().post_event(*this, make<ResizeEvent>(old_rect, m_rect));
}

void Window::add_child_window(Window& child_window)
{
    m_child_windows.append(child_window.make_weak_ptr());
}

void Window::set_parent_window(Window& parent_window)
{
    ASSERT(!m_parent_window);
    m_parent_window = parent_window.make_weak_ptr();
    parent_window.add_child_window(*this);
}

void Window::set_progress(int progress)
{
    if (m_progress == progress)
        return;

    m_progress = progress;
    WindowManager::the().notify_progress_changed(*this);
}

}
