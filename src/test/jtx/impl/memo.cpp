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

#include <test/jtx/memo.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/protocol/jss.h>

namespace ripple {
namespace test {
namespace jtx {

void
memo::operator()(Env&, JTx& jt) const
{
    auto& jv = jt.jv;
    auto& ma = jv["Memos"];
    auto& mi = ma[ma.size()];
    auto& m = mi["Memo"];
    m["MemoData"] = strHex(data_);
    m["MemoFormat"] = strHex(format_);
    m["MemoType"] = strHex(type_);
}

void
memodata::operator()(Env&, JTx& jt) const
{
    auto& jv = jt.jv;
    auto& ma = jv["Memos"];
    auto& mi = ma[ma.size()];
    auto& m = mi["Memo"];
    m["MemoData"] = strHex(s_);
}

void
memoformat::operator()(Env&, JTx& jt) const
{
    auto& jv = jt.jv;
    auto& ma = jv["Memos"];
    auto& mi = ma[ma.size()];
    auto& m = mi["Memo"];
    m["MemoFormat"] = strHex(s_);
}

void
memotype::operator()(Env&, JTx& jt) const
{
    auto& jv = jt.jv;
    auto& ma = jv["Memos"];
    auto& mi = ma[ma.size()];
    auto& m = mi["Memo"];
    m["MemoType"] = strHex(s_);
}

void
memondata::operator()(Env&, JTx& jt) const
{
    auto& jv = jt.jv;
    auto& ma = jv["Memos"];
    auto& mi = ma[ma.size()];
    auto& m = mi["Memo"];
    m["MemoFormat"] = strHex(format_);
    m["MemoType"] = strHex(type_);
}

void
memonformat::operator()(Env&, JTx& jt) const
{
    auto& jv = jt.jv;
    auto& ma = jv["Memos"];
    auto& mi = ma[ma.size()];
    auto& m = mi["Memo"];
    m["MemoData"] = strHex(data_);
    m["MemoType"] = strHex(type_);
}

void
memontype::operator()(Env&, JTx& jt) const
{
    auto& jv = jt.jv;
    auto& ma = jv["Memos"];
    auto& mi = ma[ma.size()];
    auto& m = mi["Memo"];
    m["MemoData"] = strHex(data_);
    m["MemoFormat"] = strHex(format_);
}


} // jtx
} // test
} // ripple
