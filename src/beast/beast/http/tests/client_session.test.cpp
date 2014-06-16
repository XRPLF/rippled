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

// LIBS: pthread
// MODULES: urls_large_data.cpp ../impl/raw_parser.cpp ../impl/joyent_parser.cpp

#if BEAST_INCLUDE_BEASTCONFIG
#include "../../BeastConfig.h"
#endif

#include <beast/unit_test/suite.h>

#include <beast/http/tests/urls_large_data.h>
#include <beast/http/client_session.h>
#include <beast/http/get.h>
#include <beast/asio/bind_handler.h>
#include <beast/asio/memory_buffer.h>
#include <beast/utility/ci_char_traits.h>

#include <boost/asio.hpp>

#include <thread>
#include <unordered_map>

namespace beast {
namespace http {

/** Allows thread-safe forward traversal of a sequence.
    
    Each time the shared_iterator is dereferenced it provides an element in
    the sequence or the one-past-the-end iterator if there are no elements
    remaining in the sequence. Access to the shared iterator is thread safe:
    multiple threads of execution can request iterators from the sequence,
    and no two threads will see the same iterator.

    Any operations on the underlying container which would invalidate
    iterators or change the sequence of elements pointed to by the range
    of iterators referenced by the shared_iterator, results in undefined
    behavior.
*/
template <class Iterator>
class shared_iterator
{
public:
    static_assert (std::is_same <Iterator, std::decay_t <Iterator>>::value,
        "Iterator may not be a reference or const type");

    typedef Iterator value_type;

private:
    std::mutex m_mutex;
    Iterator m_iter;
    Iterator m_end;

public:
    /** Construct the iteration from the range [first, last) */
    shared_iterator (Iterator first, Iterator last)
        : m_iter (first)
        , m_end (last)
    {
    }

    /** Obtains the next iterator in the sequence.
        Post-condition
            Current shared position in the sequence is advanced by one.
        Thread safety:
            Can be called from any thread at any time.
    */
    Iterator
    operator* ()
    {
        std::lock_guard <decltype(m_mutex)> lock (m_mutex);
        if (m_iter == m_end)
            return m_iter;
        return m_iter++;
    }

    /** Returns the one-past-the end iterator for the sequence.
        Thread safety:
            Can be called from any thread at any time.
    */
    Iterator
    end() const
    {
        return m_end;
    }
};

//------------------------------------------------------------------------------

class client_session_test : public unit_test::suite
{
public:
    typedef boost::system::error_code error_code;

    //--------------------------------------------------------------------------

    /** Used to submit HTTP requests. */
    class Request
    {
    private:
        typedef std::string value_type;
        typedef std::string string_type;

        std::unordered_map <std::string, string_type> m_headers;

        // This also works, for allowing header values to
        // span multiple discontiguous memory buffers.
        //
        std::unordered_map <std::string,
            std::vector <std::string>> m_headers_plus;

        class vector_proxy
        {
        private:
            std::reference_wrapper <std::vector <string_type>> m_vec;

        public:
            explicit vector_proxy (std::vector <string_type>& vec)
                : m_vec (vec)
            {
            }

            vector_proxy& operator= (string_type const& s)
            {
                m_vec.get().emplace_back (s);
                return *this;
            }
        };

    public:
        bool keep_alive()
        {
            return false;
        }

        // Use this to set the fields
        std::string&
        operator[] (std::string const& field)
        {
            return m_headers[field];
        }

        /** Calls Function for each header.

            Requirements:

            `X` The type `Function`
            `Y` A type meeting this requirement:
                    ConvertibleToConstBuffer
            `Z` A type meeting either requirement:
                    ConstBufferSequence
                    ConvertibleToConstBuffer
            `f` A value of type `X`
            `n` A value of type `Y`
            `v` A value of type `Z`

            The expression

                f (n, v);

            must be well formed.
        */
        template <class Function>
        void
        headers (Function f)
        {
            for (auto const& h : m_headers)
                f (h.first, h.second);
        }
    };

    //--------------------------------------------------------------------------

    class Response
    {
    private:
        typedef boost::system::error_code error_code;
        asio::memory_buffer <std::uint8_t> m_buffer;
        boost::asio::streambuf m_body;

    public:
        enum
        {
            buffer_bytes = 4192
        };

        typedef std::vector <std::pair <
            std::string, std::string>> headers_type;

        headers_type headers;

        Response()
            : m_buffer (buffer_bytes)
        {
        }

        boost::asio::mutable_buffer
        buffer()
        {
            return boost::asio::mutable_buffer (
                m_buffer.data(), m_buffer.size());
        }

        template <class FieldString, class ValueString>
        error_code
        header (FieldString const& field, ValueString const& value)
        {
            headers.push_back (std::make_pair (field, value));
            return error_code();
        }

        error_code
        body (boost::asio::const_buffer in)
        {
            m_body.commit (boost::asio::buffer_copy (
                m_body.prepare (boost::asio::buffer_size (in)),
                    boost::asio::buffer (in)));
            return error_code();
        }

        std::size_t
        size() const
        {
            return m_body.size();
        }

        std::string
        data() const
        {
            std::string s;
            s.resize (m_body.size());
            boost::asio::buffer_copy (boost::asio::buffer (
                &s[0], s.size()), m_body.data());
            return s;
        }
    };

    //--------------------------------------------------------------------------

    template <class Session>
    error_code
    visit (Session& session, std::string const& url)
    {
        error_code ec;
        typedef boost::asio::ip::tcp::resolver resolver_t;
        boost::asio::io_service ios;
        resolver_t r (ios);
        auto iter (r.resolve (resolver_t::query (
            url, "80", resolver_t::query::numeric_service), ec));

        if (ec)
            return ec;

        if (iter != resolver_t::iterator())
        {
            session.next_layer().connect (iter->endpoint(), ec);
            if (ec)
                return ec;

            Request req;
            req ["User-Agent"] = "rippled-http-client/1.0";
            req ["Host"] = url + ":80";
            req ["Content-Type"] = "application/text";
            req ["Accept"] = "application/text";

            //req ["Content-length"] = "0";
            //req.prepare ("GET / HTTP/1.0");

            Response resp;
            ec = session.get (req, resp);
            
            if (ec)
            {
                // hack
                session.next_layer().close();
            }

            log <<
                "GET " << url << " " << ec.message();

            for (auto const& h : resp.headers)
                log << h.first << ": " << h.second;

            log << resp.data();
            log << " ";
        }

        return ec;
    }

    //--------------------------------------------------------------------------

    template <class Iterator>
    void
    concurrent_get (shared_iterator <Iterator>& iter)
    {
        typedef boost::asio::ip::tcp::socket socket_type;
        for (auto cur (*iter); cur != iter.end(); cur = *iter)
        {
            sync_client_session <socket_type> session;
            std::string const base (*cur);
            std::string url;
            url = "www." + base;
            auto const ec (visit (session, url));
        }
    }

    // Perform HTTP get on a sequence of URLs in parallel
    // Requirements
    //  Sequence must me
    //  Sequence::value_type must be convertible to std::string
    template <class Iterator>
    void test_concurrent_get (Iterator first, Iterator last)
    {
#if 0
last = first;
std::advance (last, 3000);
#endif

        shared_iterator <Iterator> iter (first, last);

        std::vector <std::thread> pool;
#if 0
        std::size_t const hardware_concurrency (
            std::max (std::thread::hardware_concurrency(),
            2u
            ));
#else
        std::size_t const hardware_concurrency (1);
#endif
        
        for (std::size_t n (hardware_concurrency); n--;)
            pool.emplace_back (std::bind (
                &client_session_test::concurrent_get <Iterator>, this,
                    std::ref (iter)));

        for (auto& t : pool)
            t.join();

        pass();
    }

    template <class Sequence>
    void test_concurrent_get (Sequence const& sequence)
    {
        auto last (std::begin(sequence));
        std::advance (last, std::min (std::size_t(1), sequence.size()));
        test_concurrent_get (std::begin (sequence), last);
    }

    void test_get()
    {
        get ("http://www.google.com");
    }

    void run()
    {
        //test_get();
        test_concurrent_get (urls_large_data());
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(client_session,http,beast);

}
}
