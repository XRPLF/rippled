#pragma once

#include <test/jtx/Env.h>

#include <utility>

namespace xrpl::test::jtx {

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
    memo(std::string data, std::string format, std::string type)
        : data_(std::move(data)), format_(std::move(format)), type_(std::move(type))
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
    memo_data(std::string s) : s_(std::move(s))
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
    memo_format(std::string s) : s_(std::move(s))
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
    memo_type(std::string s) : s_(std::move(s))
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

}  // namespace xrpl::test::jtx
