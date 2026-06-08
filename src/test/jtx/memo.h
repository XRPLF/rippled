#pragma once

#include <test/jtx/Env.h>

#include <utility>

namespace xrpl::test::jtx {

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
    Memo(std::string data, std::string format, std::string type)
        : data_(std::move(data)), format_(std::move(format)), type_(std::move(type))
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
    MemoData(std::string s) : s_(std::move(s))
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
    MemoFormat(std::string s) : s_(std::move(s))
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
    MemoType(std::string s) : s_(std::move(s))
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

}  // namespace xrpl::test::jtx
