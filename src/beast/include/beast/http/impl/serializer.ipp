//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_IMPL_SERIALIZER_IPP
#define BEAST_HTTP_IMPL_SERIALIZER_IPP

#include <beast/core/detail/buffers_ref.hpp>
#include <beast/http/error.hpp>
#include <beast/http/status.hpp>
#include <beast/core/detail/config.hpp>
#include <boost/assert.hpp>
#include <ostream>

namespace beast {
namespace http {

template<bool isRequest, class Body,
    class Fields, class ChunkDecorator>
void
serializer<isRequest, Body, Fields, ChunkDecorator>::
frdinit(std::true_type)
{
    frd_.emplace(m_, m_.version, m_.method());
}

template<bool isRequest, class Body,
    class Fields, class ChunkDecorator>
void
serializer<isRequest, Body, Fields, ChunkDecorator>::
frdinit(std::false_type)
{
    frd_.emplace(m_, m_.version, m_.result_int());
}

template<bool isRequest, class Body,
    class Fields, class ChunkDecorator>
template<class T1, class T2, class Visit>
inline
void
serializer<isRequest, Body, Fields, ChunkDecorator>::
do_visit(error_code& ec, Visit& visit)
{
    // VFALCO work-around for missing variant::emplace
    pv_.~variant();
    new(&pv_) decltype(pv_){
        T1{limit_, boost::get<T2>(v_)}};
    visit(ec, beast::detail::make_buffers_ref(
        boost::get<T1>(pv_)));
}

//------------------------------------------------------------------------------

template<bool isRequest, class Body,
    class Fields, class ChunkDecorator>
serializer<isRequest, Body, Fields, ChunkDecorator>::
serializer(value_type& m)
    : m_(m)
    , rd_(m_)
{
}

template<bool isRequest, class Body,
    class Fields, class ChunkDecorator>
serializer<isRequest, Body, Fields, ChunkDecorator>::
serializer(value_type& m, ChunkDecorator const& d)
    : m_(m)
    , rd_(m_)
    , d_(d)
{
}

template<bool isRequest, class Body,
    class Fields, class ChunkDecorator>
template<class Visit>
void
serializer<isRequest, Body, Fields, ChunkDecorator>::
next(error_code& ec, Visit&& visit)
{
    using boost::asio::buffer_size;
    using beast::detail::make_buffers_ref;
    switch(s_)
    {
    case do_construct:
    {
        frdinit(std::integral_constant<bool,
            isRequest>{});
        keep_alive_ = m_.keep_alive();
        chunked_ = m_.chunked();
        if(chunked_)
            goto go_init_c;
        s_ = do_init;
        BEAST_FALLTHROUGH;
    }

    case do_init:
    {
        rd_.init(ec);
        if(ec)
            return;
        if(split_)
            goto go_header_only;
        auto result = rd_.get(ec);
        if(ec == error::need_more)
            goto go_header_only;
        if(ec)
            return;
        if(! result)
            goto go_header_only;
        more_ = result->second;
        v_ = cb2_t{
            boost::in_place_init,
            frd_->get(),
            result->first};
        s_ = do_header;
        BEAST_FALLTHROUGH;
    }

    case do_header:
        do_visit<pcb2_t, cb2_t>(ec, visit);
        break;

    go_header_only:
        v_ = cb1_t{frd_->get()};
        s_ = do_header_only;
    case do_header_only:
        do_visit<pcb1_t, cb1_t>(ec, visit);
        break;

    case do_body:
        s_ = do_body + 1;
        BEAST_FALLTHROUGH;

    case do_body + 1:
    {
        auto result = rd_.get(ec);
        if(ec)
            return;
        if(! result)
            goto go_complete;
        more_ = result->second;
        v_ = cb3_t{result->first};
        s_ = do_body + 2;
        BEAST_FALLTHROUGH;
    }

    case do_body + 2:
        do_visit<pcb3_t, cb3_t>(ec, visit);
        break;

    //----------------------------------------------------------------------

         go_init_c:
    s_ = do_init_c;
    case do_init_c:
    {
        rd_.init(ec);
        if(ec)
            return;
        if(split_)
            goto go_header_only_c;
        auto result = rd_.get(ec);
        if(ec == error::need_more)
            goto go_header_only_c;
        if(ec)
            return;
        if(! result)
            goto go_header_only_c;
        more_ = result->second;
    #ifndef BEAST_NO_BIG_VARIANTS
        if(! more_)
        {
            // do it all in one buffer
            v_ = cb7_t{
                boost::in_place_init,
                frd_->get(),
                detail::chunk_header{
                    buffer_size(result->first)},
                [&]()
                {
                    auto sv = d_(result->first);
                    return boost::asio::const_buffers_1{
                        sv.data(), sv.size()};
                
                }(),
                detail::chunk_crlf(),
                result->first,
                detail::chunk_crlf(),
                detail::chunk_final(),
                [&]()
                {
                    auto sv = d_(
                        boost::asio::null_buffers{});
                    return boost::asio::const_buffers_1{
                        sv.data(), sv.size()};
                
                }(),
                detail::chunk_crlf()};
            goto go_all_c;
        }
    #endif
        v_ = cb4_t{
            boost::in_place_init,
            frd_->get(),
            detail::chunk_header{
                buffer_size(result->first)},
            [&]()
            {
                auto sv = d_(result->first);
                return boost::asio::const_buffers_1{
                    sv.data(), sv.size()};
                
            }(),
            detail::chunk_crlf(),
            result->first,
            detail::chunk_crlf()};
        s_ = do_header_c;
        BEAST_FALLTHROUGH;
    }

    case do_header_c:
        do_visit<pcb4_t, cb4_t>(ec, visit);
        break;

    go_header_only_c:
        v_ = cb1_t{frd_->get()};
        s_ = do_header_only_c;
    case do_header_only_c:
        do_visit<pcb1_t, cb1_t>(ec, visit);
        break;

    case do_body_c:
        s_ = do_body_c + 1;
        BEAST_FALLTHROUGH;

    case do_body_c + 1:
    {
        auto result = rd_.get(ec);
        if(ec)
            return;
        if(! result)
            goto go_final_c;
        more_ = result->second;
    #ifndef BEAST_NO_BIG_VARIANTS
        if(! more_)
        {
            // do it all in one buffer
            v_ = cb6_t{
                boost::in_place_init,
                detail::chunk_header{
                    buffer_size(result->first)},
                [&]()
                {
                    auto sv = d_(result->first);
                    return boost::asio::const_buffers_1{
                        sv.data(), sv.size()};
                
                }(),
                detail::chunk_crlf(),
                result->first,
                detail::chunk_crlf(),
                detail::chunk_final(),
                [&]()
                {
                    auto sv = d_(
                        boost::asio::null_buffers{});
                    return boost::asio::const_buffers_1{
                        sv.data(), sv.size()};
                
                }(),
                detail::chunk_crlf()};
            goto go_body_final_c;
        }
    #endif
        v_ = cb5_t{
            boost::in_place_init,
            detail::chunk_header{
                buffer_size(result->first)},
            [&]()
            {
                auto sv = d_(result->first);
                return boost::asio::const_buffers_1{
                    sv.data(), sv.size()};
                
            }(),
            detail::chunk_crlf(),
            result->first,
            detail::chunk_crlf()};
        s_ = do_body_c + 2;
        BEAST_FALLTHROUGH;
    }

    case do_body_c + 2:
        do_visit<pcb5_t, cb5_t>(ec, visit);
        break;

#ifndef BEAST_NO_BIG_VARIANTS
    go_body_final_c:
        s_ = do_body_final_c;
    case do_body_final_c:
        do_visit<pcb6_t, cb6_t>(ec, visit);
        break;

    go_all_c:
        s_ = do_all_c;
    case do_all_c:
        do_visit<pcb7_t, cb7_t>(ec, visit);
        break;
#endif

    go_final_c:
    case do_final_c:
        v_ = cb8_t{
            boost::in_place_init,
            detail::chunk_final(),
            [&]()
            {
                auto sv = d_(
                    boost::asio::null_buffers{});
                return boost::asio::const_buffers_1{
                    sv.data(), sv.size()};
                
            }(),
            detail::chunk_crlf()};
        s_ = do_final_c + 1;
        BEAST_FALLTHROUGH;

    case do_final_c + 1:
        do_visit<pcb8_t, cb8_t>(ec, visit);
        break;

    //----------------------------------------------------------------------

    default:
    case do_complete:
        BOOST_ASSERT(false);
        break;

    go_complete:
        s_ = do_complete;
        break;
    }
}

template<bool isRequest, class Body,
    class Fields, class ChunkDecorator>
void
serializer<isRequest, Body, Fields, ChunkDecorator>::
consume(std::size_t n)
{
    using boost::asio::buffer_size;
    switch(s_)
    {
    case do_header:
        BOOST_ASSERT(n <= buffer_size(
            boost::get<cb2_t>(v_)));
        boost::get<cb2_t>(v_).consume(n);
        if(buffer_size(boost::get<cb2_t>(v_)) > 0)
            break;
        header_done_ = true;
        v_ = boost::blank{};
        if(! more_)
            goto go_complete;
        s_ = do_body + 1;
        break;

    case do_header_only:
        BOOST_ASSERT(n <= buffer_size(
            boost::get<cb1_t>(v_)));
        boost::get<cb1_t>(v_).consume(n);
        if(buffer_size(boost::get<cb1_t>(v_)) > 0)
            break;
        frd_ = boost::none;
        header_done_ = true;
        if(! split_)
            goto go_complete;
        s_ = do_body;
        break;

    case do_body + 2:
    {
        BOOST_ASSERT(n <= buffer_size(
            boost::get<cb3_t>(v_)));
        boost::get<cb3_t>(v_).consume(n);
        if(buffer_size(boost::get<cb3_t>(v_)) > 0)
            break;
        v_ = boost::blank{};
        if(! more_)
            goto go_complete;
        s_ = do_body + 1;
        break;
    }

    //----------------------------------------------------------------------

    case do_header_c:
        BOOST_ASSERT(n <= buffer_size(
            boost::get<cb4_t>(v_)));
        boost::get<cb4_t>(v_).consume(n);
        if(buffer_size(boost::get<cb4_t>(v_)) > 0)
            break;
        header_done_ = true;
        v_ = boost::blank{};
        if(more_)
            s_ = do_body_c + 1;
        else
            s_ = do_final_c;
        break;

    case do_header_only_c:
    {
        BOOST_ASSERT(n <= buffer_size(
            boost::get<cb1_t>(v_)));
        boost::get<cb1_t>(v_).consume(n);
        if(buffer_size(boost::get<cb1_t>(v_)) > 0)
            break;
        frd_ = boost::none;
        header_done_ = true;
        if(! split_)
        {
            s_ = do_final_c;
            break;
        }
        s_ = do_body_c;
        break;
    }

    case do_body_c + 2:
        BOOST_ASSERT(n <= buffer_size(
            boost::get<cb5_t>(v_)));
        boost::get<cb5_t>(v_).consume(n);
        if(buffer_size(boost::get<cb5_t>(v_)) > 0)
            break;
        v_ = boost::blank{};
        if(more_)
            s_ = do_body_c + 1;
        else
            s_ = do_final_c;
        break;

#ifndef BEAST_NO_BIG_VARIANTS
    case do_body_final_c:
    {
        auto& b = boost::get<cb6_t>(v_);
        BOOST_ASSERT(n <= buffer_size(b));
        b.consume(n);
        if(buffer_size(b) > 0)
            break;
        v_ = boost::blank{};
        s_ = do_complete;
        break;
    }

    case do_all_c:
    {
        auto& b = boost::get<cb7_t>(v_);
        BOOST_ASSERT(n <= buffer_size(b));
        b.consume(n);
        if(buffer_size(b) > 0)
            break;
        header_done_ = true;
        v_ = boost::blank{};
        s_ = do_complete;
        break;
    }
#endif

    case do_final_c + 1:
        BOOST_ASSERT(buffer_size(
            boost::get<cb8_t>(v_)));
        boost::get<cb8_t>(v_).consume(n);
        if(buffer_size(boost::get<cb8_t>(v_)) > 0)
            break;
        v_ = boost::blank{};
        goto go_complete;

    //----------------------------------------------------------------------

    default:
        BOOST_ASSERT(false);
    case do_complete:
        break;

    go_complete:
        s_ = do_complete;
        break;
    }
}

} // http
} // beast

#endif
