/*
 * Copyright (c) 2020, Andreas Kling <kling@serenityos.org>
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

#include <AK/FlyString.h>
#include <AK/Function.h>
#include <LibJS/Interpreter.h>
#include <LibJS/Runtime/Error.h>
#include <LibJS/Runtime/Function.h>
#include <LibJS/Runtime/Shape.h>
#include <LibWeb/Bindings/DocumentWrapper.h>
#include <LibWeb/Bindings/LocationObject.h>
#include <LibWeb/Bindings/NavigatorObject.h>
#include <LibWeb/Bindings/WindowObject.h>
#include <LibWeb/Bindings/XMLHttpRequestConstructor.h>
#include <LibWeb/Bindings/XMLHttpRequestPrototype.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/DOM/Window.h>

namespace Web {
namespace Bindings {

WindowObject::WindowObject(Window& impl)
    : m_impl(impl)
{
}

void WindowObject::initialize()
{
    GlobalObject::initialize();

    define_property("window", this, JS::Attribute::Enumerable);
    define_native_property("document", document_getter, document_setter, JS::Attribute::Enumerable);
    define_native_function("alert", alert);
    define_native_function("confirm", confirm);
    define_native_function("setInterval", set_interval, 1);
    define_native_function("setTimeout", set_timeout, 1);
    define_native_function("requestAnimationFrame", request_animation_frame, 1);
    define_native_function("cancelAnimationFrame", cancel_animation_frame, 1);

    define_property("navigator", heap().allocate<NavigatorObject>(), JS::Attribute::Enumerable | JS::Attribute::Configurable);
    define_property("location", heap().allocate<LocationObject>(), JS::Attribute::Enumerable | JS::Attribute::Configurable);

    m_xhr_prototype = heap().allocate<XMLHttpRequestPrototype>();
    m_xhr_constructor = heap().allocate<XMLHttpRequestConstructor>();
    m_xhr_constructor->define_property("prototype", m_xhr_prototype, 0);
    add_constructor("XMLHttpRequest", m_xhr_constructor, *m_xhr_prototype);
}

WindowObject::~WindowObject()
{
}

void WindowObject::visit_children(Visitor& visitor)
{
    GlobalObject::visit_children(visitor);
    visitor.visit(m_xhr_constructor);
    visitor.visit(m_xhr_prototype);
}

static Window* impl_from(JS::Interpreter& interpreter)
{
    auto* this_object = interpreter.this_value().to_object(interpreter);
    if (!this_object) {
        ASSERT_NOT_REACHED();
        return nullptr;
    }
    if (StringView("WindowObject") != this_object->class_name()) {
        interpreter.throw_exception<JS::TypeError>("That's not a WindowObject, bro.");
        return nullptr;
    }
    return &static_cast<WindowObject*>(this_object)->impl();
}

JS::Value WindowObject::alert(JS::Interpreter& interpreter)
{
    auto* impl = impl_from(interpreter);
    if (!impl)
        return {};
    String message = "";
    if (interpreter.argument_count()) {
        message = interpreter.argument(0).to_string(interpreter);
        if (interpreter.exception())
            return {};
    }
    impl->alert(message);
    return JS::js_undefined();
}

JS::Value WindowObject::confirm(JS::Interpreter& interpreter)
{
    auto* impl = impl_from(interpreter);
    if (!impl)
        return {};
    String message = "";
    if (interpreter.argument_count()) {
        message = interpreter.argument(0).to_string(interpreter);
        if (interpreter.exception())
            return {};
    }
    return JS::Value(impl->confirm(message));
}

JS::Value WindowObject::set_interval(JS::Interpreter& interpreter)
{
    auto* impl = impl_from(interpreter);
    if (!impl)
        return {};
    if (!interpreter.argument_count())
        return interpreter.throw_exception<JS::TypeError>("setInterval() needs at least one argument");
    auto* callback_object = interpreter.argument(0).to_object(interpreter);
    if (!callback_object)
        return {};
    if (!callback_object->is_function())
        return interpreter.throw_exception<JS::TypeError>("Not a function");

    i32 interval = 0;
    if (interpreter.argument_count() >= 2) {
        interval = interpreter.argument(1).to_i32(interpreter);
        if (interpreter.exception())
            return {};
        if (interval < 0)
            interval = 0;
    }

    impl->set_interval(*static_cast<JS::Function*>(callback_object), interval);
    return JS::js_undefined();
}

JS::Value WindowObject::set_timeout(JS::Interpreter& interpreter)
{
    auto* impl = impl_from(interpreter);
    if (!impl)
        return {};
    if (!interpreter.argument_count())
        return interpreter.throw_exception<JS::TypeError>("setTimeout() needs at least one argument");
    auto* callback_object = interpreter.argument(0).to_object(interpreter);
    if (!callback_object)
        return {};
    if (!callback_object->is_function())
        return interpreter.throw_exception<JS::TypeError>("Not a function");

    i32 interval = 0;
    if (interpreter.argument_count() >= 2) {
        interval = interpreter.argument(1).to_i32(interpreter);
        if (interpreter.exception())
            return {};
        if (interval < 0)
            interval = 0;
    }

    impl->set_timeout(*static_cast<JS::Function*>(callback_object), interval);
    return JS::js_undefined();
}

JS::Value WindowObject::request_animation_frame(JS::Interpreter& interpreter)
{
    auto* impl = impl_from(interpreter);
    if (!impl)
        return {};
    if (!interpreter.argument_count())
        return interpreter.throw_exception<JS::TypeError>("requestAnimationFrame() needs one argument");
    auto* callback_object = interpreter.argument(0).to_object(interpreter);
    if (!callback_object)
        return {};
    if (!callback_object->is_function())
        return interpreter.throw_exception<JS::TypeError>("Not a function");
    return JS::Value(impl->request_animation_frame(*static_cast<JS::Function*>(callback_object)));
}

JS::Value WindowObject::cancel_animation_frame(JS::Interpreter& interpreter)
{
    auto* impl = impl_from(interpreter);
    if (!impl)
        return {};
    if (!interpreter.argument_count())
        return interpreter.throw_exception<JS::TypeError>("cancelAnimationFrame() needs one argument");
    auto id = interpreter.argument(0).to_i32(interpreter);
    if (interpreter.exception())
        return {};
    impl->cancel_animation_frame(id);
    return JS::js_undefined();
}

JS::Value WindowObject::document_getter(JS::Interpreter& interpreter)
{
    auto* impl = impl_from(interpreter);
    if (!impl)
        return {};
    return wrap(interpreter.heap(), impl->document());
}

void WindowObject::document_setter(JS::Interpreter&, JS::Value)
{
    // FIXME: Figure out what we should do here. Just ignore attempts to set window.document for now.
}

}
}
