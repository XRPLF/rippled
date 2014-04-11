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

#ifndef BEAST_ASIO_HANDSHAKE_HANDSHAKEDETECTLOGIC_H_INCLUDED
#define BEAST_ASIO_HANDSHAKE_HANDSHAKEDETECTLOGIC_H_INCLUDED

namespace beast {
namespace asio {

class HandshakeDetectLogic
{
public:
    HandshakeDetectLogic ()
        : m_finished (false)
        , m_success (false)
    {
    }

    /** How many bytes maximum we might need.

        This is the largest number of bytes that the detector
        might need in order to come to a conclusion about
        whether or not the handshake is a match. Depending
        on the data, it could come to that conclusion sooner
        though.

        Use read_some instead of read so that the detect logic
        can reject the handshake sooner if possible.
    */
    virtual std::size_t max_needed () = 0;

    /** How many bytes the handshake consumes.
        If the detector processes the entire handshake this will
        be non zero. The SSL detector would return 0, since we
        want all the existing bytes to be passed on.
    */
    virtual std::size_t bytes_consumed () = 0;

    /** Return true if we have enough data to form a conclusion.
    */
    bool finished () const noexcept
    {
        return m_finished;
    }

    /** Return true if we came to a conclusion and the data matched.
    */
    bool success () const noexcept
    {
        return m_finished && m_success;
    }

protected:
    void conclude (bool success = true)
    {
        m_finished = true;
        m_success = success;
    }

    void fail ()
    {
        conclude (false);
    }

private:
    bool m_finished;
    bool m_success;
};

//------------------------------------------------------------------------------

/** Wraps the logic and exports it as an abstract interface.
*/
template <typename Logic>
class HandshakeDetectLogicType
{
public:
    typedef Logic LogicType;
    typedef typename Logic::arg_type arg_type;

    explicit HandshakeDetectLogicType (arg_type const& arg = arg_type ())
        : m_logic (arg)
    {
    }

    LogicType& get ()
    {
        return m_logic;
    }

    std::size_t max_needed ()
    {
        return m_logic.max_needed ();
    }

    std::size_t bytes_consumed ()
    {
        return m_logic.bytes_consumed ();
    }

    bool finished ()
    {
        return m_logic.finished ();
    }

    /** If finished is true, this tells us if the handshake was detected.
    */
    bool success ()
    {
        return m_logic.success ();
    }

    /** Analyze the buffer to match the Handshake.
        Returns `true` if the analysis is complete.
    */
    template <typename ConstBufferSequence>
    bool analyze (ConstBufferSequence const& buffer)
    {
        bassert (! m_logic.finished ());
        m_logic.analyze (buffer);
        return m_logic.finished ();
    }

private:
    Logic m_logic;
};

}
}

#endif
