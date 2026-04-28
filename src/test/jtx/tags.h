#pragma once

namespace xrpl::test::jtx {

struct none_t
{
    none_t() = default;
};
static none_t const none;

struct autofill_t
{
    autofill_t() = default;
};
static autofill_t const autofill;

struct disabled_t
{
    disabled_t() = default;
};
static disabled_t const disabled;

/** Used for fee() calls that use an owner reserve increment */
struct increment_t
{
    increment_t() = default;
};

static increment_t const increment;

}  // namespace xrpl::test::jtx
