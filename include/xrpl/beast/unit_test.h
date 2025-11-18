#ifndef BEAST_UNIT_TEST_H_INCLUDED
#define BEAST_UNIT_TEST_H_INCLUDED

#include <xrpl/beast/unit_test/amount.h>
#include <xrpl/beast/unit_test/global_suites.h>
#include <xrpl/beast/unit_test/match.h>
#include <xrpl/beast/unit_test/recorder.h>
#include <xrpl/beast/unit_test/reporter.h>
#include <xrpl/beast/unit_test/results.h>
#include <xrpl/beast/unit_test/runner.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/unit_test/suite_info.h>
#include <xrpl/beast/unit_test/suite_list.h>

#ifndef BEAST_EXPECT
#define BEAST_EXPECT_S1(x) #x
#define BEAST_EXPECT_S2(x) BEAST_EXPECT_S1(x)
// #define BEAST_EXPECT(cond) {expect(cond, __FILE__ ":" ##
//__LINE__);}while(false){}
#define BEAST_EXPECT(cond) expect(cond, __FILE__ ":" BEAST_EXPECT_S2(__LINE__))
#endif

#endif
