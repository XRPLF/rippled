//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================
//
// Context
//
//------------------------------------------------------------------------------

HandlerCall::Context::Context (Call* call) noexcept
    : m_call (call)
{
    bassert (m_call != nullptr);
}

HandlerCall::Context::Context () noexcept
    : m_call (nullptr)
{
}

HandlerCall::Context::Context (Context const& other) noexcept
    : m_call (other.m_call)
{
}

HandlerCall::Context::Context (HandlerCall const& handler) noexcept
    : m_call (handler.m_call.get ())
{
}

HandlerCall::Context& HandlerCall::Context::operator= (Context other) noexcept
{
    m_call = other.m_call;
    return *this;
}

bool HandlerCall::Context::operator== (Call const* call) const noexcept
{
    return m_call == call;
}

bool HandlerCall::Context::operator!= (Call const* call) const noexcept
{
    return m_call != call;
}

bool HandlerCall::Context::isComposed () const noexcept
{
    return m_call->is_continuation ();
}

bool HandlerCall::Context::isNull () const noexcept
{
    return m_call == nullptr;
}

bool HandlerCall::Context::isNotNull () const noexcept
{
    return m_call != nullptr;
}

bool HandlerCall::Context::operator== (Context other) const noexcept
{
    return m_call == other.m_call;
}

bool HandlerCall::Context::operator!= (Context other) const noexcept
{
    return m_call != other.m_call;
}

void* HandlerCall::Context::allocate (std::size_t size) const
{
    return m_call->allocate (size);
}

void HandlerCall::Context::deallocate (void* p, std::size_t size) const
{
    m_call->deallocate (p, size);
}

//------------------------------------------------------------------------------
//
// Call
//
//------------------------------------------------------------------------------

HandlerCall::Call::Call (Context context) noexcept
    : m_context (context.isNull () ? Context (this) : context)
    , m_is_continuation (false)
    , m_is_final_continuation (false)
{
}

HandlerCall::Call::~Call ()
{
}

HandlerCall::Context HandlerCall::Call::getContext () const noexcept
{
    return m_context;
}

bool HandlerCall::Call::is_continuation () const noexcept
{
    // If this goes off it means someone isn't calling getContext()!
    bassert (m_context == this);
    return m_is_continuation;
}

void HandlerCall::Call::set_continuation () noexcept
{
    // Setting it twice means some code is sloppy!
    bassert (! m_is_continuation);

    m_is_continuation = true;
}

void HandlerCall::Call::set_final_continuation () noexcept
{
    // Soemone called endComposed without calling beginComposed!
    bassert (m_is_continuation);
    // When true, we will clear
    // m_is_continuation on our next completion
    m_is_final_continuation = true;
}

void HandlerCall::Call::check_continuation () noexcept
{
    if (m_is_final_continuation)
    {
        bassert (m_is_continuation);

        m_is_continuation = false;
        m_is_final_continuation = false;
    }
}

void HandlerCall::Call::operator() ()
{
    check_continuation ();
    dispatch ();
}

void HandlerCall::Call::operator() (error_code const& ec)
{
    check_continuation ();
    dispatch (ec);
}

void HandlerCall::Call::operator() (error_code const& ec, std::size_t bytes_transferred)
{
    check_continuation ();
    dispatch (ec, bytes_transferred);
}

void HandlerCall::Call::dispatch ()
{
    pure_virtual_called ();
}

void HandlerCall::Call::dispatch (error_code const&)
{
    pure_virtual_called ();
}

void HandlerCall::Call::dispatch (error_code const&, std::size_t)
{
    pure_virtual_called ();
}

void* HandlerCall::Call::pure_virtual_called ()
{
    // These shouldn't be getting called. But since the object returned
    // by most implementations of bind have operator() up to high arity
    // levels, it is not generally possible to write a traits test that
    // works in all scenarios for detecting a particular signature of a
    // handler.
    //
    fatal_error ("pure virtual called");
    return nullptr;
}

//------------------------------------------------------------------------------
//
// HandlerCall
//
//------------------------------------------------------------------------------

HandlerCall::HandlerCall () noexcept
{
}

HandlerCall::HandlerCall (HandlerCall const& other) noexcept
    : m_call (other.m_call)
{ 
}

HandlerCall& HandlerCall::operator= (HandlerCall const& other) noexcept
{
    m_call = other.m_call;
    return *this;
}

#if BEAST_COMPILER_SUPPORTS_MOVE_SEMANTICS
HandlerCall::HandlerCall (HandlerCall&& other) noexcept
    : m_call (other.m_call)
{
    other.m_call = nullptr;
}

HandlerCall& HandlerCall::operator= (HandlerCall&& other) noexcept
{
    m_call = other.m_call;
    other.m_call = nullptr;
    return *this;
}
#endif

bool HandlerCall::isNull () const noexcept
{
    return m_call == nullptr;
}

bool HandlerCall::isNotNull () const noexcept
{
    return m_call != nullptr;
}

HandlerCall::Context HandlerCall::getContext () const noexcept
{
    bassert (m_call != nullptr);
    return m_call->getContext ();
}

bool HandlerCall::isFinal () const noexcept
{
    return m_call->getContext () == m_call.get ();
}

HandlerCall& HandlerCall::beginComposed () noexcept
{
    // If this goes off it means that your handler is
    // already sharing a context with another handler!
    // You have to call beginComposed on the original handler.
    //
    bassert (isFinal ());
    m_call->set_continuation ();
    return *this;
}

HandlerCall& HandlerCall::endComposed () noexcept
{
    // If this goes off it means that your handler is
    // already sharing a context with another handler!
    // You have to call beginComposed on the original handler.
    //
    bassert (isFinal ());
    m_call->set_final_continuation ();
    return *this;
}

void HandlerCall::operator() ()
{
    (*m_call)();
}

void HandlerCall::operator() (error_code const& ec)
{
    (*m_call)(ec);
}

void HandlerCall::operator() (error_code const& ec, std::size_t bytes_transferred)
{
    (*m_call)(ec, bytes_transferred);
}

//------------------------------------------------------------------------------
//
// Specializations
//
//------------------------------------------------------------------------------

void* asio_handler_allocate (std::size_t size, HandlerCall* call)
{
    // Always go through the call's context.
    return call->getContext ().allocate (size);
}

void* asio_handler_allocate (std::size_t size, HandlerCall::Call* call)
{
    // Always go through the call's context.
    return call->getContext ().allocate (size);
}

void* asio_handler_allocate (std::size_t size, HandlerCall::Context* context)
{
    return context->allocate (size);
}

//------------------------------------------------------------------------------

void asio_handler_deallocate (void* p, std::size_t size, HandlerCall* call)
{
    // Always go through the call's context.
    call->getContext ().deallocate (p, size);
}

void asio_handler_deallocate (void* p, std::size_t size, HandlerCall::Call* call)
{
    // Always go through the call's context.
    call->getContext ().deallocate (p, size);
}

void asio_handler_deallocate (void* p, std::size_t size, HandlerCall::Context* context)
{
    context->deallocate (p, size);
}

//------------------------------------------------------------------------------

bool asio_handler_is_continuation (HandlerCall* call)
{
    return call->getContext().isComposed ();
}

bool asio_handler_is_continuation (HandlerCall::Call* call)
{
    return call->getContext().isComposed ();
}

bool asio_handler_is_continuation (HandlerCall::Context*)
{
    // Something is horribly wrong if we're trying to
    // use a Context as a completion handler?
    //
    fatal_error ("A function was unexpectedly called.");
    return false;
}
