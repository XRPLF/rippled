#ifndef XRPL_TEST_JTX_MEMO_H_INCLUDED
#define XRPL_TEST_JTX_MEMO_H_INCLUDED

#include <test/jtx/Env.h>

namespace xrpl {
namespace test {
namespace jtx {

/** Add a memo to a JTx.

    If a memo already exists, the new
    memo is appended to the array.
*/
class memo
{
private:
    std::string data_;
    std::string format_;
    std::string type_;

public:
    memo(
        std::string const& data,
        std::string const& format,
        std::string const& type)
        : data_(data), format_(format), type_(type)
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

class memo_data
{
private:
    std::string s_;

public:
    memo_data(std::string const& s) : s_(s)
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

class memo_format
{
private:
    std::string s_;

public:
    memo_format(std::string const& s) : s_(s)
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

class memo_type
{
private:
    std::string s_;

public:
    memo_type(std::string const& s) : s_(s)
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

}  // namespace jtx
}  // namespace test
}  // namespace xrpl

#endif
