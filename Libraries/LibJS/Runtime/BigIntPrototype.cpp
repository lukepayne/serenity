/*
 * Copyright (c) 2020, Linus Groh <mail@linusgroh.de>
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

#include <LibJS/Interpreter.h>
#include <LibJS/Runtime/BigIntObject.h>
#include <LibJS/Runtime/BigIntPrototype.h>
#include <LibJS/Runtime/Error.h>
#include <LibJS/Runtime/GlobalObject.h>

namespace JS {

BigIntPrototype::BigIntPrototype()
    : Object(interpreter().global_object().object_prototype())
{
    u8 attr = Attribute::Writable | Attribute::Configurable;
    define_native_function("toString", to_string, 0, attr);
    define_native_function("toLocaleString", to_locale_string, 0, attr);
    define_native_function("valueOf", value_of, 0, attr);
}

BigIntPrototype::~BigIntPrototype()
{
}

static BigIntObject* bigint_object_from(Interpreter& interpreter)
{
    auto* this_object = interpreter.this_value().to_object(interpreter);
    if (!this_object)
        return nullptr;
    if (!this_object->is_bigint_object()) {
        interpreter.throw_exception<TypeError>("Not a BigInt object");
        return nullptr;
    }
    return static_cast<BigIntObject*>(this_object);
}

Value BigIntPrototype::to_string(Interpreter& interpreter)
{
    auto* bigint_object = bigint_object_from(interpreter);
    if (!bigint_object)
        return {};
    return js_string(interpreter, bigint_object->bigint().big_integer().to_base10());
}

Value BigIntPrototype::to_locale_string(Interpreter& interpreter)
{
    return to_string(interpreter);
}

Value BigIntPrototype::value_of(Interpreter& interpreter)
{
    auto* bigint_object = bigint_object_from(interpreter);
    if (!bigint_object)
        return {};
    return bigint_object->value_of();
}

}
