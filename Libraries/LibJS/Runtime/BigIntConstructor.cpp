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

#include <AK/String.h>
#include <LibCrypto/BigInt/SignedBigInteger.h>
#include <LibJS/Interpreter.h>
#include <LibJS/Runtime/BigIntConstructor.h>
#include <LibJS/Runtime/BigIntObject.h>
#include <LibJS/Runtime/Error.h>
#include <LibJS/Runtime/GlobalObject.h>

namespace JS {

BigIntConstructor::BigIntConstructor()
    : NativeFunction("BigInt", *interpreter().global_object().function_prototype())
{
    define_property("prototype", interpreter().global_object().bigint_prototype(), 0);
    define_property("length", Value(1), Attribute::Configurable);

    u8 attr = Attribute::Writable | Attribute::Configurable;
    define_native_function("asIntN", as_int_n, 2, attr);
    define_native_function("asUintN", as_uint_n, 2, attr);
}

BigIntConstructor::~BigIntConstructor()
{
}

Value BigIntConstructor::call(Interpreter& interpreter)
{
    auto primitive = interpreter.argument(0).to_primitive(interpreter, Value::PreferredType::Number);
    if (interpreter.exception())
        return {};
    if (primitive.is_number()) {
        if (!primitive.is_integer()) {
            interpreter.throw_exception<RangeError>("BigInt argument must be an integer");
            return {};
        }
        return js_bigint(interpreter, Crypto::SignedBigInteger { primitive.as_i32() });
    }
    auto* bigint = interpreter.argument(0).to_bigint(interpreter);
    if (interpreter.exception())
        return {};
    return bigint;
}

Value BigIntConstructor::construct(Interpreter& interpreter)
{
    interpreter.throw_exception<TypeError>("BigInt is not a constructor");
    return {};
}

Value BigIntConstructor::as_int_n(Interpreter&)
{
    TODO();
}

Value BigIntConstructor::as_uint_n(Interpreter&)
{
    TODO();
}

}
