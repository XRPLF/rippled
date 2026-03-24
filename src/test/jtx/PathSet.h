#pragma once

#include <test/jtx.h>

#include <xrpl/basics/Log.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/TxFlags.h>

namespace xrpl {
namespace test {

/** Count offer
 */
inline std::size_t
countOffers(
    jtx::Env& env,
    jtx::Account const& account,
    Issue const& takerPays,
    Issue const& takerGets)
{
    size_t count = 0;
    forEachItem(*env.current(), account, [&](std::shared_ptr<SLE const> const& sle) {
        if (sle->getType() == ltOFFER && sle->getFieldAmount(sfTakerPays).issue() == takerPays &&
            sle->getFieldAmount(sfTakerGets).issue() == takerGets)
            ++count;
    });
    return count;
}

inline std::size_t
countOffers(
    jtx::Env& env,
    jtx::Account const& account,
    STAmount const& takerPays,
    STAmount const& takerGets)
{
    size_t count = 0;
    forEachItem(*env.current(), account, [&](std::shared_ptr<SLE const> const& sle) {
        if (sle->getType() == ltOFFER && sle->getFieldAmount(sfTakerPays) == takerPays &&
            sle->getFieldAmount(sfTakerGets) == takerGets)
            ++count;
    });
    return count;
}

/** An offer exists
 */
inline bool
isOffer(
    jtx::Env& env,
    jtx::Account const& account,
    STAmount const& takerPays,
    STAmount const& takerGets)
{
    return countOffers(env, account, takerPays, takerGets) > 0;
}

/** An offer exists
 */
inline bool
isOffer(jtx::Env& env, jtx::Account const& account, Issue const& takerPays, Issue const& takerGets)
{
    return countOffers(env, account, takerPays, takerGets) > 0;
}

class TestPath
{
public:
    STPath path;

    TestPath() = default;
    TestPath(TestPath const&) = default;
    TestPath&
    operator=(TestPath const&) = default;
    TestPath(TestPath&&) = default;
    TestPath&
    operator=(TestPath&&) = default;

    template <class First, class... Rest>
    explicit TestPath(First&& first, Rest&&... rest)
    {
        addHelper(std::forward<First>(first), std::forward<Rest>(rest)...);
    }
    TestPath&
    push_back(Issue const& iss);
    TestPath&
    push_back(jtx::Account const& acc);
    TestPath&
    push_back(STPathElement const& pe);
    Json::Value
    json() const;

private:
    template <class First, class... Rest>
    void
    addHelper(First&& first, Rest&&... rest);
};

inline TestPath&
TestPath::push_back(STPathElement const& pe)
{
    path.emplace_back(pe);
    return *this;
}

inline TestPath&
TestPath::push_back(Issue const& iss)
{
    path.emplace_back(
        STPathElement::typeCurrency | STPathElement::typeIssuer,
        beast::zero,
        iss.currency,
        iss.account);
    return *this;
}

inline TestPath&
TestPath::push_back(jtx::Account const& account)
{
    path.emplace_back(account.id(), beast::zero, beast::zero);
    return *this;
}

template <class First, class... Rest>
void
TestPath::addHelper(First&& first, Rest&&... rest)
{
    push_back(std::forward<First>(first));
    if constexpr (sizeof...(rest) > 0)
        addHelper(std::forward<Rest>(rest)...);
}

inline Json::Value
TestPath::json() const
{
    return path.getJson(JsonOptions::kNONE);
}

class PathSet
{
public:
    STPathSet paths;

    PathSet() = default;
    PathSet(PathSet const&) = default;
    PathSet&
    operator=(PathSet const&) = default;
    PathSet(PathSet&&) = default;
    PathSet&
    operator=(PathSet&&) = default;

    template <class First, class... Rest>
    explicit PathSet(First&& first, Rest&&... rest)
    {
        addHelper(std::forward<First>(first), std::forward<Rest>(rest)...);
    }
    Json::Value
    json() const
    {
        Json::Value v;
        v["Paths"] = paths.getJson(JsonOptions::kNONE);
        return v;
    }

private:
    template <class First, class... Rest>
    void
    addHelper(First first, Rest... rest)
    {
        paths.emplace_back(std::move(first.path));
        if constexpr (sizeof...(rest) > 0)
            addHelper(std::move(rest)...);
    }
};

}  // namespace test
}  // namespace xrpl
