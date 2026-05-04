#pragma once

#include <test/jtx/Env.h>

#include <xrpl/protocol/Issue.h>

#include <type_traits>

namespace xrpl {
class STPath;

namespace test::jtx {

/** Set Paths, SendMax on a JTx. */
class Paths
{
private:
    Asset in_;
    int depth_;
    unsigned int limit_;

public:
    Paths(Asset const& in, int depth = 7, unsigned int limit = 4)
        : in_(in), depth_(depth), limit_(limit)
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

//------------------------------------------------------------------------------

/** Add a path.

    If no paths are present, a new one is created.
*/
class Path
{
private:
    json::Value jv_;

public:
    Path();

    template <class T, class... Args>
    explicit Path(T const& t, Args const&... args);

    Path(STPath const& p);

    void
    operator()(Env&, JTx& jt) const;

private:
    json::Value&
    create();

    void
    appendOne(Account const& account);

    void
    appendOne(AccountID const& account);

    template <class T>
    std::enable_if_t<std::is_constructible_v<Account, T>>
    appendOne(T const& t)
    {
        appendOne(Account{t});
    }

    void
    appendOne(IOU const& iou);

    void
    appendOne(BookSpec const& book);

    template <class T, class... Args>
    void
    append(T const& t, Args const&... args);
};

template <class T, class... Args>
Path::Path(T const& t, Args const&... args) : jv_(json::ArrayValue)
{
    append(t, args...);
}

template <class T, class... Args>
void
Path::append(T const& t, Args const&... args)
{
    appendOne(t);
    if constexpr (sizeof...(args) > 0)
        append(args...);
}

}  // namespace test::jtx

}  // namespace xrpl
