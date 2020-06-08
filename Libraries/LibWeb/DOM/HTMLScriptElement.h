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

#pragma once

#include <AK/Function.h>
#include <LibWeb/DOM/HTMLElement.h>

namespace Web {

class HTMLScriptElement : public HTMLElement {
public:
    HTMLScriptElement(Document&, const FlyString& tag_name);
    virtual ~HTMLScriptElement() override;

    virtual void inserted_into(Node&) override;
    virtual void children_changed() override;

    bool is_non_blocking() const { return m_non_blocking; }
    bool is_ready_to_be_parser_executed() const { return m_ready_to_be_parser_executed; }

    void set_parser_document(Badge<HTMLDocumentParser>, Document&);
    void set_non_blocking(Badge<HTMLDocumentParser>, bool);
    void set_already_started(Badge<HTMLDocumentParser>, bool b) { m_already_started = b; }
    void prepare_script(Badge<HTMLDocumentParser>);
    void execute_script();

private:
    void script_became_ready();
    void when_the_script_is_ready(Function<void()>);

    WeakPtr<Document> m_parser_document;
    WeakPtr<Document> m_preparation_time_document;
    bool m_non_blocking { false };
    bool m_already_started { false };
    bool m_parser_inserted { false };
    bool m_from_an_external_file { false };
    bool m_script_ready { false };
    bool m_ready_to_be_parser_executed { false };

    Function<void()> m_script_ready_callback;

    String m_script_source;
};

template<>
inline bool is<HTMLScriptElement>(const Node& node)
{
    return is<Element>(node) && to<Element>(node).tag_name() == HTML::TagNames::script;
}

}
