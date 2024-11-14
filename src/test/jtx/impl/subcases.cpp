//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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

#include <test/jtx/subcases.h>

#include <stdexcept>

namespace subcases {

Subcase::Subcase(Context& context, char const* name)
    : context_(context), name_(name)
{
    lastCreated = this;
}

Subcase::operator bool() const
{
    auto& _ = context_;
    ++_.level;
    if (_.level >= MAXIMUM_SUBCASE_DEPTH)
        throw std::logic_error("maximum subcase depth exceeded");
    if (_.entered < _.level && _.skip[_.level] == _.skipped)
    {
        _.entered = _.level;
        _.names[_.level] = name_;
        _.skipped = 0;
        return true;
    }
    ++_.skipped;
    return false;
}

Subcase::~Subcase()
{
    auto& _ = context_;
    if (_.level == _.entered && _.skipped == 0)
    {
        // We are destroying the leaf subcase that executed on this pass.
        // We call `suite::testcase()` here, after the subcase is finished,
        // because only now do we know which subcase was the leaf,
        // and we only want to print one name line for each subcase.
        _.suite.testcase(_.name());
        // Let the runner know that a test executed,
        // even if `BEAST_EXPECT` was never called.
        _.suite.pass();
    }
    if (_.skipped == 0)
    {
        ++_.skip[_.level];
        _.skip[_.level + 1] = 0;
    }
    --_.level;
}

void
execute(beast::unit_test::suite* suite, char const* name, Supercase supercase)
{
    Context context{*suite};
    context.names[0] = name;
    do
    {
        context.lap();
        supercase(context);
    } while (context.skipped != 0);
}

}  // namespace subcases
