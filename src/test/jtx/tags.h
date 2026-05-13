#pragma once

namespace xrpl::test::jtx {

struct NoneT
{
    NoneT() = default;
};
static NoneT const kNONE;

struct AutofillT
{
    AutofillT() = default;
};
static AutofillT const kAUTOFILL;

struct DisabledT
{
    DisabledT() = default;
};
static DisabledT const kDISABLED;

/** Used for Fee() calls that use an owner reserve increment */
struct IncrementT
{
    IncrementT() = default;
};

static IncrementT const kINCREMENT;

}  // namespace xrpl::test::jtx
