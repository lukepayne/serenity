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

#include <LibJS/Interpreter.h>
#include <LibJS/Runtime/Shape.h>

namespace JS {

Shape* Shape::create_unique_clone() const
{
    auto* new_shape = heap().allocate<Shape>();
    new_shape->m_unique = true;
    new_shape->m_prototype = m_prototype;
    ensure_property_table();
    new_shape->ensure_property_table();
    (*new_shape->m_property_table) = *m_property_table;
    return new_shape;
}

Shape* Shape::create_put_transition(const FlyString& property_name, PropertyAttributes attributes)
{
    TransitionKey key { property_name, attributes };
    if (auto* existing_shape = m_forward_transitions.get(key).value_or(nullptr))
        return existing_shape;
    auto* new_shape = heap().allocate<Shape>(this, property_name, attributes, TransitionType::Put);
    m_forward_transitions.set(key, new_shape);
    return new_shape;
}

Shape* Shape::create_configure_transition(const FlyString& property_name, PropertyAttributes attributes)
{
    TransitionKey key { property_name, attributes };
    if (auto* existing_shape = m_forward_transitions.get(key).value_or(nullptr))
        return existing_shape;
    auto* new_shape = heap().allocate<Shape>(this, property_name, attributes, TransitionType::Configure);
    m_forward_transitions.set(key, new_shape);
    return new_shape;
}

Shape* Shape::create_prototype_transition(Object* new_prototype)
{
    return heap().allocate<Shape>(this, new_prototype);
}

Shape::Shape()
{
}

Shape::Shape(Shape* previous_shape, const FlyString& property_name, PropertyAttributes attributes, TransitionType transition_type)
    : m_previous(previous_shape)
    , m_property_name(property_name)
    , m_attributes(attributes)
    , m_prototype(previous_shape->m_prototype)
    , m_transition_type(transition_type)
{
}

Shape::Shape(Shape* previous_shape, Object* new_prototype)
    : m_previous(previous_shape)
    , m_prototype(new_prototype)
    , m_transition_type(TransitionType::Prototype)
{
}

Shape::~Shape()
{
}

void Shape::visit_children(Cell::Visitor& visitor)
{
    Cell::visit_children(visitor);
    visitor.visit(m_prototype);
    visitor.visit(m_previous);
    for (auto& it : m_forward_transitions)
        visitor.visit(it.value);
}

Optional<PropertyMetadata> Shape::lookup(const FlyString& property_name) const
{
    auto property = property_table().get(property_name);
    if (!property.has_value())
        return {};
    return property;
}

const HashMap<FlyString, PropertyMetadata>& Shape::property_table() const
{
    ensure_property_table();
    return *m_property_table;
}

size_t Shape::property_count() const
{
    return property_table().size();
}

Vector<Shape::Property> Shape::property_table_ordered() const
{
    auto vec = Vector<Shape::Property>();
    vec.resize(property_table().size());

    for (auto& it : property_table()) {
        vec[it.value.offset] = { it.key, it.value };
    }

    return vec;
}

void Shape::ensure_property_table() const
{
    if (m_property_table)
        return;
    m_property_table = make<HashMap<FlyString, PropertyMetadata>>();

    // FIXME: We need to make sure the GC doesn't collect the transition chain as we're building it.
    //        Maybe some kind of RAII "prevent GC for a moment" helper thingy?

    Vector<const Shape*> transition_chain;
    for (auto* shape = this; shape->m_previous; shape = shape->m_previous) {
        transition_chain.append(shape);
    }

    u32 next_offset = 0;
    for (ssize_t i = transition_chain.size() - 1; i >= 0; --i) {
        auto* shape = transition_chain[i];
        if (shape->m_property_name.is_null()) {
            // Ignore prototype transitions as they don't affect the key map.
            continue;
        }
        if (shape->m_transition_type == TransitionType::Put) {
            m_property_table->set(shape->m_property_name, { next_offset++, shape->m_attributes });
        } else if (shape->m_transition_type == TransitionType::Configure) {
            auto it = m_property_table->find(shape->m_property_name);
            ASSERT(it != m_property_table->end());
            it->value.attributes = shape->m_attributes;
        }
    }
}

void Shape::add_property_to_unique_shape(const FlyString& property_name, PropertyAttributes attributes)
{
    ASSERT(is_unique());
    ASSERT(m_property_table);
    ASSERT(!m_property_table->contains(property_name));
    m_property_table->set(property_name, { m_property_table->size(), attributes });
}

void Shape::reconfigure_property_in_unique_shape(const FlyString& property_name, PropertyAttributes attributes)
{
    ASSERT(is_unique());
    ASSERT(m_property_table);
    ASSERT(m_property_table->contains(property_name));
    m_property_table->set(property_name, { m_property_table->size(), attributes });
}

void Shape::remove_property_from_unique_shape(const FlyString& property_name, size_t offset)
{
    ASSERT(is_unique());
    ASSERT(m_property_table);
    m_property_table->remove(property_name);
    for (auto& it : *m_property_table) {
        ASSERT(it.value.offset != offset);
        if (it.value.offset > offset)
            --it.value.offset;
    }
}

}
