#ifndef XRPL_TEST_JTX_TAGS_H_INCLUDED
#define XRPL_TEST_JTX_TAGS_H_INCLUDED

namespace ripple {
namespace test {

namespace jtx {

struct none_t
{
    none_t()
    {
    }
};
static none_t const none;

struct autofill_t
{
    autofill_t()
    {
    }
};
static autofill_t const autofill;

struct disabled_t
{
    disabled_t()
    {
    }
};
static disabled_t const disabled;

/** Used for fee() calls that use an owner reserve increment */
struct increment_t
{
    increment_t()
    {
    }
};

static increment_t const increment;

}  // namespace jtx

}  // namespace test
}  // namespace ripple

#endif
