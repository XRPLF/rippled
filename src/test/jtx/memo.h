#pragma once

#include <test/jtx/Env.h>

namespace xrpl {
namespace test {
namespace jtx {

/** Add a memo to a JTx.

    If a memo already exists, the new
    memo is appended to the array.
*/
class Memo
{
private:
    std::string data_;
    std::string format_;
    std::string type_;

public:
    Memo(std::string const& data, std::string const& format, std::string const& type)
        : data_(data), format_(format), type_(type)
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

class MemoData
{
private:
    std::string s_;

public:
    MemoData(std::string const& s) : s_(s)
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

class MemoFormat
{
private:
    std::string s_;

public:
    MemoFormat(std::string const& s) : s_(s)
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

class MemoType
{
private:
    std::string s_;

public:
    MemoType(std::string const& s) : s_(s)
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

}  // namespace jtx
}  // namespace test
}  // namespace xrpl
