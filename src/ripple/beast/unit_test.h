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

#ifndef BEAST_UNIT_TEST_H_INCLUDED
#define BEAST_UNIT_TEST_H_INCLUDED

#include <beast/unit_test/amount.hpp>
#include <beast/unit_test/global_suites.hpp>
#include <beast/unit_test/match.hpp>
#include <beast/unit_test/recorder.hpp>
#include <beast/unit_test/reporter.hpp>
#include <beast/unit_test/results.hpp>
#include <beast/unit_test/runner.hpp>
#include <beast/unit_test/suite.hpp>
#include <beast/unit_test/suite_info.hpp>
#include <beast/unit_test/suite_list.hpp>

#ifndef BEAST_EXPECT
#define BEAST_EXPECT_S1(x) #x
#define BEAST_EXPECT_S2(x) BEAST_EXPECT_S1(x)
//#define BEAST_EXPECT(cond) {expect(cond, __FILE__ ":" ##
//__LINE__);}while(false){}
#define BEAST_EXPECT(cond) expect(cond, __FILE__ ":" BEAST_EXPECT_S2(__LINE__))
#endif

#endif
