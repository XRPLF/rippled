#pragma once

#include <tx/wasm/fixtures/FloatConstants.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

// The float constants with a real ledger and GTest attached, for the `host_functions/Float*`
// tests. The constants alone are in FloatConstants.h, which links no test framework.

namespace xrpl::test {

struct FloatTest : RealHostFixture, FloatConstants
{
};

}  // namespace xrpl::test
