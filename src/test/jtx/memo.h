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

#ifndef RIPPLE_TEST_JTX_MEMO_H_INCLUDED
#define RIPPLE_TEST_JTX_MEMO_H_INCLUDED

#include <test/jtx/Env.h>

namespace ripple {
namespace test {
namespace jtx {

/** Add a memo to a JTx.

    If a memo already exists, the new
    memo is appended to the array.
*/
class memo
{
private:
    std::string data_;
    std::string format_;
    std::string type_;

public:
    memo(
        std::string const& data,
        std::string const& format,
        std::string const& type)
        : data_(data), format_(format), type_(type)
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

class memodata
{
private:
    std::string s_;

public:
    memodata(std::string const& s) : s_(s)
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

class memoformat
{
private:
    std::string s_;

public:
    memoformat(std::string const& s) : s_(s)
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

class memotype
{
private:
    std::string s_;

public:
    memotype(std::string const& s) : s_(s)
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

class memondata
{
private:
    std::string format_;
    std::string type_;

public:
    memondata(std::string const& format, std::string const& type)
        : format_(format), type_(type)
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

class memonformat
{
private:
    std::string data_;
    std::string type_;

public:
    memonformat(std::string const& data, std::string const& type)
        : data_(data), type_(type)
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

class memontype
{
private:
    std::string data_;
    std::string format_;

public:
    memontype(std::string const& data, std::string const& format)
        : data_(data), format_(format)
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
