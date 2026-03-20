#pragma once

#include <test/jtx/Env.h>

#include <xrpl/protocol/Issue.h>

#include <type_traits>

namespace xrpl {
namespace test {
namespace jtx {

/** Set Paths, SendMax on a JTx. */
class Paths
{
private:
    Issue in_;
    int depth_;
    unsigned int limit_;

public:
    Paths(Issue const& in, int depth = 7, unsigned int limit = 4)
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
    Json::Value jv_;

public:
    Path();

    template <class T, class... Args>
    explicit Path(T const& t, Args const&... args);

    void
    operator()(Env&, JTx& jt) const;

private:
    Json::Value&
    create();

    void
    appendOne(Account const& account);

    void
    appendOne(AccountID const& account);

    template <class T>
    std::enable_if_t<std::is_constructible<Account, T>::value>
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
Path::Path(T const& t, Args const&... args) : jv_(Json::arrayValue)
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

}  // namespace jtx
}  // namespace test
}  // namespace xrpl
