#pragma once

namespace xrpl::test::jtx {

struct NoneT
{
    NoneT() = default;
};
static NoneT const kNone;

struct AutofillT
{
    AutofillT() = default;
};
static AutofillT const kAutofill;

struct DisabledT
{
    DisabledT() = default;
};
static DisabledT const kDisabled;

/** Used for Fee() calls that use an owner reserve increment */
struct IncrementT
{
    IncrementT() = default;
};

static IncrementT const kIncrement;

}  // namespace xrpl::test::jtx
