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

#include "TaskbarWindow.h"
#include "TaskbarButton.h"
#include <AK/SharedBuffer.h>
#include <LibCore/ConfigFile.h>
#include <LibCore/StandardPaths.h>
#include <LibGUI/BoxLayout.h>
#include <LibGUI/Button.h>
#include <LibGUI/Desktop.h>
#include <LibGUI/Frame.h>
#include <LibGUI/Painter.h>
#include <LibGUI/Window.h>
#include <LibGfx/Palette.h>
#include <stdio.h>

//#define EVENT_DEBUG

class TaskbarWidget final : public GUI::Widget {
    C_OBJECT(TaskbarWidget);

public:
    virtual ~TaskbarWidget() override {}

private:
    TaskbarWidget() {}

    virtual void paint_event(GUI::PaintEvent& event) override
    {
        GUI::Painter painter(*this);
        painter.add_clip_rect(event.rect());
        painter.fill_rect(rect(), palette().button());
        painter.draw_line({ 0, 1 }, { width() - 1, 1 }, palette().threed_highlight());
    }
};

TaskbarWindow::TaskbarWindow()
{
    set_window_type(GUI::WindowType::Taskbar);
    set_title("Taskbar");

    on_screen_rect_change(GUI::Desktop::the().rect());

    GUI::Desktop::the().on_rect_change = [this](const Gfx::Rect& rect) { on_screen_rect_change(rect); };

    auto& widget = set_main_widget<TaskbarWidget>();
    widget.set_layout<GUI::HorizontalBoxLayout>();
    widget.layout()->set_margins({ 3, 2, 3, 2 });
    widget.layout()->set_spacing(3);

    m_default_icon = Gfx::Bitmap::load_from_file("/res/icons/16x16/window.png");

    WindowList::the().aid_create_button = [this](auto& identifier) {
        return create_button(identifier);
    };

    create_quick_launch_bar();
}

TaskbarWindow::~TaskbarWindow()
{
}

void TaskbarWindow::create_quick_launch_bar()
{
    auto& quick_launch_bar = main_widget()->add<GUI::Frame>();
    quick_launch_bar.set_size_policy(GUI::SizePolicy::Fixed, GUI::SizePolicy::Fixed);
    quick_launch_bar.set_layout<GUI::HorizontalBoxLayout>();
    quick_launch_bar.layout()->set_spacing(3);
    quick_launch_bar.layout()->set_margins({ 3, 0, 3, 0 });
    quick_launch_bar.set_frame_thickness(0);

    int total_width = 6;
    bool first = true;

    auto config = Core::ConfigFile::get_for_app("Taskbar");
    constexpr const char* quick_launch = "QuickLaunch";

    // FIXME: Core::ConfigFile does not keep the order of the entries.
    for (auto& name : config->keys(quick_launch)) {
        auto af_name = config->read_entry(quick_launch, name);
        ASSERT(!af_name.is_null());
        auto af_path = String::format("/res/apps/%s", af_name.characters());
        auto af = Core::ConfigFile::open(af_path);
        auto app_executable = af->read_entry("App", "Executable");
        auto app_icon_path = af->read_entry("Icons", "16x16");

        auto& button = quick_launch_bar.add<GUI::Button>();
        button.set_size_policy(GUI::SizePolicy::Fixed, GUI::SizePolicy::Fixed);
        button.set_preferred_size(22, 22);
        button.set_button_style(Gfx::ButtonStyle::CoolBar);

        button.set_icon(Gfx::Bitmap::load_from_file(app_icon_path));
        button.set_tooltip(name);
        button.on_click = [app_executable](auto) {
            pid_t pid = fork();
            if (pid < 0) {
                perror("fork");
            } else if (pid == 0) {
                if (chdir(Core::StandardPaths::home_directory().characters()) < 0) {
                    perror("chdir");
                    exit(1);
                }
                execl(app_executable.characters(), app_executable.characters(), nullptr);
                perror("execl");
                ASSERT_NOT_REACHED();
            }
        };

        if (!first)
            total_width += 3;
        first = false;
        total_width += 22;
    }

    quick_launch_bar.set_preferred_size(total_width, 22);
}

void TaskbarWindow::on_screen_rect_change(const Gfx::Rect& rect)
{
    Gfx::Rect new_rect { rect.x(), rect.bottom() - taskbar_height() + 1, rect.width(), taskbar_height() };
    set_rect(new_rect);
}

NonnullRefPtr<GUI::Button> TaskbarWindow::create_button(const WindowIdentifier& identifier)
{
    auto& button = main_widget()->add<TaskbarButton>(identifier);
    button.set_size_policy(GUI::SizePolicy::Fixed, GUI::SizePolicy::Fixed);
    button.set_preferred_size(140, 22);
    button.set_text_alignment(Gfx::TextAlignment::CenterLeft);
    button.set_icon(*m_default_icon);
    return button;
}

static bool should_include_window(GUI::WindowType window_type, bool is_frameless)
{
    return window_type == GUI::WindowType::Normal && !is_frameless;
}

void TaskbarWindow::wm_event(GUI::WMEvent& event)
{
    WindowIdentifier identifier { event.client_id(), event.window_id() };
    switch (event.type()) {
    case GUI::Event::WM_WindowRemoved: {
#ifdef EVENT_DEBUG
        auto& removed_event = static_cast<GUI::WMWindowRemovedEvent&>(event);
        dbgprintf("WM_WindowRemoved: client_id=%d, window_id=%d\n",
            removed_event.client_id(),
            removed_event.window_id());
#endif
        WindowList::the().remove_window(identifier);
        update();
        break;
    }
    case GUI::Event::WM_WindowRectChanged: {
#ifdef EVENT_DEBUG
        auto& changed_event = static_cast<GUI::WMWindowRectChangedEvent&>(event);
        dbgprintf("WM_WindowRectChanged: client_id=%d, window_id=%d, rect=%s\n",
            changed_event.client_id(),
            changed_event.window_id(),
            changed_event.rect().to_string().characters());
#endif
        break;
    }

    case GUI::Event::WM_WindowIconBitmapChanged: {
        auto& changed_event = static_cast<GUI::WMWindowIconBitmapChangedEvent&>(event);
#ifdef EVENT_DEBUG
        dbgprintf("WM_WindowIconBitmapChanged: client_id=%d, window_id=%d, icon_buffer_id=%d\n",
            changed_event.client_id(),
            changed_event.window_id(),
            changed_event.icon_buffer_id());
#endif
        if (auto* window = WindowList::the().window(identifier)) {
            auto buffer = SharedBuffer::create_from_shbuf_id(changed_event.icon_buffer_id());
            ASSERT(buffer);
            window->button()->set_icon(Gfx::Bitmap::create_with_shared_buffer(Gfx::BitmapFormat::RGBA32, *buffer, changed_event.icon_size()));
        }
        break;
    }

    case GUI::Event::WM_WindowStateChanged: {
        auto& changed_event = static_cast<GUI::WMWindowStateChangedEvent&>(event);
#ifdef EVENT_DEBUG
        dbgprintf("WM_WindowStateChanged: client_id=%d, window_id=%d, title=%s, rect=%s, is_active=%u, is_minimized=%u\n",
            changed_event.client_id(),
            changed_event.window_id(),
            changed_event.title().characters(),
            changed_event.rect().to_string().characters(),
            changed_event.is_active(),
            changed_event.is_minimized());
#endif
        if (!should_include_window(changed_event.window_type(), changed_event.is_frameless()))
            break;
        auto& window = WindowList::the().ensure_window(identifier);
        window.set_title(changed_event.title());
        window.set_rect(changed_event.rect());
        window.set_active(changed_event.is_active());
        window.set_minimized(changed_event.is_minimized());
        window.set_progress(changed_event.progress());
        if (window.is_minimized()) {
            window.button()->set_foreground_color(Color::DarkGray);
            window.button()->set_text(String::format("[%s]", changed_event.title().characters()));
        } else {
            window.button()->set_foreground_color(Color::Black);
            window.button()->set_text(changed_event.title());
        }
        window.button()->set_checked(changed_event.is_active());
        break;
    }
    default:
        break;
    }
}
