#pragma once

#include <xrpl/beast/unit_test/suite.h>

#ifndef BEAST_EXPECT
#define BEAST_EXPECT_S1(x) #x
#define BEAST_EXPECT_S2(x) BEAST_EXPECT_S1(x)
// #define BEAST_EXPECT(cond) {expect(cond, __FILE__ ":" ##
//__LINE__);}while(false){}
#define BEAST_EXPECT(cond) expect(cond, __FILE__ ":" BEAST_EXPECT_S2(__LINE__))
#endif
