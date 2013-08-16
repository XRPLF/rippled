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

#ifndef BEAST_HANDSHAKEDETECTORTYPE_H_INCLUDED
#define BEAST_HANDSHAKEDETECTORTYPE_H_INCLUDED

class DetectPolicy
{
public:
    DetectPolicy ()
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
    */
    virtual std::size_t max_needed () = 0;

    /** Returns true if the return value of success() is valid.
    */
    bool finished () const noexcept
    {
        return m_finished;
    }

    /** Returns true if the buffers matched the Handshake
    */
    bool success () const noexcept
    {
        bassert (m_finished);
        return m_success;
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

    //--------------------------------------------------------------------------

    /** Represents a small, fixed size buffer.
        This provides a convenient interface for doing a bytewise
        verification/reject test on a handshake protocol.
    */
    template <int Bytes>
    struct Input
    {
        template <typename ConstBufferSequence>
        explicit Input (ConstBufferSequence const& buffer)
            : m_buffer (boost::asio::buffer (m_storage))
            , m_size (boost::asio::buffer_copy (m_buffer, buffer))
            , m_data (boost::asio::buffer_cast <uint8 const*> (m_buffer))
        {
        }
#if 0
        uint8 const* data () const noexcept
        {
            return m_data;
        }
#endif

        uint8 operator[] (std::size_t index) const noexcept
        {
            bassert (index >= 0 && index < m_size);
            return m_data [index];
        }

        bool peek (std::size_t bytes) const noexcept
        {
            if (m_size >= bytes)
                return true;
            return false;
        }

        template <typename T>
        bool peek (T* t) noexcept
        {
            std::size_t const bytes = sizeof (T);
            if (m_size >= bytes)
            {
                std::copy (m_data, m_data + bytes, t);
                return true;
            }
            return false;
        }

        bool consume (std::size_t bytes) noexcept
        {
            if (m_size >= bytes)
            {
                m_data += bytes;
                m_size -= bytes;
                return true;
            }
            return false;
        }

        template <typename T>
        bool read (T* t) noexcept
        {
            std::size_t const bytes = sizeof (T);
            if (m_size >= bytes)
            {
                //this causes a stack corruption.
                //std::copy (m_data, m_data + bytes, t);

                memcpy (t, m_data, bytes);
                m_data += bytes;
                m_size -= bytes;
                return true;
            }
            return false;
        }

        // Reads an integraltype in network byte order
        template <typename IntegerType>
        bool readNetworkInteger (IntegerType* value)
        {
            // Must be an integral type!
            // not available in all versions of std:: unfortunately
            //static_bassert (std::is_integral <IntegerType>::value);
            IntegerType networkValue;
            if (! read (&networkValue))
                return false;
            *value = fromNetworkByteOrder (networkValue);
            return true;
        }

    private:
        boost::array <uint8, Bytes> m_storage;
        MutableBuffer m_buffer;
        std::size_t m_size;
        uint8 const* m_data;
    };

private:
    bool m_finished;
    bool m_success;
};

// Handshake for SSL 2
//
// http://tools.ietf.org/html/rfc5246#appendix-E.2
//
// uint8 V2CipherSpec[3];
// struct {
//    uint16 msg_length;   
//    uint8 msg_type;
//    Version version;              Should be 'ProtocolVersion'?
//    uint16 cipher_spec_length;
//    uint16 session_id_length;
//    uint16 challenge_length;
//    ...
//
class SSL2 : public DetectPolicy
{
public:
    typedef int arg_type;

    explicit SSL2 (arg_type const&)
    {
    }

    enum
    {
        bytesNeeded = 3
    };

    std::size_t max_needed ()
    {
        return bytesNeeded;
    }

    template <typename ConstBufferSequence>
    void analyze (ConstBufferSequence const& buffer)
    {
        Input <bytesNeeded> in (buffer);

        {
            uint8 byte;
            if (! in.peek (&byte))
                return;

            // First byte must have the high bit set
            //
            if((byte & 0x80) != 0x80)
            return fail ();
        }

        // The remaining bits contain the
        // length of the following data in bytes.
        //
        uint16 msg_length;
        if (! in.readNetworkInteger(&msg_length))
            return;

        // sizeof (msg_type +
        //         Version (ProtcolVersion?) +
        //         cipher_spec_length +
        //         session_id_length +
        //         challenge_length)
        //
        // Should be 9 or greater.
        //
        if (msg_length < 9)
            return fail ();

        uint8 msg_type;
        if (! in.read (&msg_type))
            return;

        // The msg_type must be 0x01 for a version 2 ClientHello
        //
        if (msg_type != 0x01)
            return fail ();

        conclude ();
    }
};

// Handshake for SSL 3 (Also TLS 1.0 and 1.1)
//
// http://www.ietf.org/rfc/rfc2246.txt
//
// Section 7.4. Handshake protocol
//
class SSL3 : public DetectPolicy
{
public:
    typedef int arg_type; // dummy

    explicit SSL3 (arg_type const&)
    {
    }

    enum
    {
        bytesNeeded = 6
    };

    std::size_t max_needed ()
    {
        return bytesNeeded;
    }

    template <typename ConstBufferSequence>
    void analyze (ConstBufferSequence const& buffer)
    {
        uint16 version;
        Input <bytesNeeded> in (buffer);

        uint8 msg_type;
        if (! in.read (&msg_type))
            return;

        // msg_type must be 0x16 = "SSL Handshake"
        //
        if (msg_type != 0x16)
            return fail ();

        if (! in.read (&version))
            return;
        version = fromNetworkByteOrder (version);

        uint16 length;
        if (! in.read (&length))
            return;

        length = fromNetworkByteOrder (length);

        conclude ();
    }
};

//--------------------------------------------------------------------------

template <typename Logic>
class HandshakeDetectorType
{
public:
    typedef typename Logic::arg_type arg_type;

    explicit HandshakeDetectorType (arg_type const& arg = arg_type ())
        : m_logic (arg)
    {
    }

    std::size_t max_needed ()  noexcept
    {
        return m_logic.max_needed ();
    }

    bool finished ()  noexcept
    {
        return m_logic.finished ();
    }

    /** If finished is true, this tells us if the handshake was detected.
    */
    bool success ()  noexcept
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

#endif
