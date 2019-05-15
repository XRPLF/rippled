//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_RESULT_H_INCLUDED
#define RIPPLE_RESULT_H_INCLUDED

#include <boost/variant.hpp>

#include <utility>

namespace ripple {

// API shamelessly copied from std::result::Result in Rust.
// https://doc.rust-lang.org/std/result/
template <typename T, typename E>
class Result {
private:
    boost::variant<T, E> value_or_error_;

    template <typename TE>
    explicit Result(TE&& value_or_error) noexcept
        : value_or_error_(std::forward<TE>(value_or_error))
    {
    }

public:
    static Result<T, E> ok(T&& value) {
        return Result(std::forward<T>(value));
    }

    static Result<T, E> err(E&& error) {
        return Result(std::forward<E>(error));
    }

    bool is_ok() const {
        return value_or_error_.which() == 0;
    }

    bool is_err() const {
        return value_or_error_.which() == 1;
    }

    T& unwrap() {
        return boost::get<T>(value_or_error_);
    }

    T const& unwrap() const {
        return boost::get<T>(value_or_error_);
    }

    E& unwrap_err() {
        return boost::get<E>(value_or_error_);
    }

    E const& unwrap_err() const {
        return boost::get<E>(value_or_error_);
    }

};

// A specialization for when the two types are the same.
// Do we need enable_if protection to compare decayed types? Why or why not?
template <typename TE>
class Result<TE, TE> {
private:
    enum Which {
        VALUE,
        ERROR
    };

    // Should we be storing the decayed type?
    TE value_or_error_;
    Which which_;

    explicit Result(TE&& value_or_error, Which which) noexcept
        : value_or_error_(std::forward<TE>(value_or_error))
        , which_(which)
    {
    }

public:
    static Result<TE, TE> ok(TE&& value) {
        return Result(value, VALUE);
    }

    static Result<TE, TE> err(TE&& error) {
        return Result(error, ERROR);
    }

    bool is_ok() const {
        return which_ == VALUE;
    }

    bool is_err() const {
        return which_ == ERROR;
    }

    TE& unwrap() {
        if (which_ != VALUE)
            // How should we handle this?
            throw std::logic_error("result is not ok");
        return value_or_error_;
    }

    TE const& unwrap() const {
        if (which_ != VALUE)
            // How should we handle this?
            throw std::logic_error("result is not ok");
        return value_or_error_;
    }

    TE& unwrap_err() {
        if (which_ != ERROR)
            // How should we handle this?
            throw std::logic_error("result is not an error");
        return value_or_error_;
    }

    TE const& unwrap_err() const {
        if (which_ != ERROR)
            // How should we handle this?
            throw std::logic_error("result is not an error");
        return value_or_error_;
    }

};

}

#endif