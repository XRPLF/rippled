#pragma once

namespace xrpl {
namespace test {

namespace jtx {

struct NoneT
{
    NoneT()
    {
    }
};
static NoneT const kNONE;

struct AutofillT
{
    AutofillT()
    {
    }
};
static AutofillT const kAUTOFILL;

struct DisabledT
{
    DisabledT()
    {
    }
};
static DisabledT const kDISABLED;

/** Used for Fee() calls that use an owner reserve kINCREMENT */
struct IncrementT
{
    IncrementT()
    {
    }
};

static IncrementT const kINCREMENT;

}  // namespace jtx

}  // namespace test
}  // namespace xrpl
