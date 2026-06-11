#pragma once

#include <test/jtx.h>

#include <xrpl/basics/Log.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/TxFlags.h>

namespace xrpl::test {

/** Count offer
 */
inline std::size_t
countOffers(
    jtx::Env& env,
    jtx::Account const& account,
    Asset const& takerPays,
    Asset const& takerGets)
{
    size_t count = 0;
    forEachItem(*env.current(), account, [&](SLE::const_ref sle) {
        if (sle->getType() == ltOFFER && sle->getFieldAmount(sfTakerPays).asset() == takerPays &&
            sle->getFieldAmount(sfTakerGets).asset() == takerGets)
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
    forEachItem(*env.current(), account, [&](SLE::const_ref sle) {
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
isOffer(jtx::Env& env, jtx::Account const& account, Asset const& takerPays, Asset const& takerGets)
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
    pushBack(Issue const& iss);
    TestPath&
    pushBack(MPTIssue const& iss);
    TestPath&
    pushBack(jtx::Account const& acc);
    TestPath&
    pushBack(STPathElement const& pe);
    [[nodiscard]] json::Value
    json() const;

private:
    template <class First, class... Rest>
    void
    addHelper(First&& first, Rest&&... rest);
};

inline TestPath&
TestPath::pushBack(STPathElement const& pe)
{
    path.emplaceBack(pe);
    return *this;
}

inline TestPath&
TestPath::pushBack(Issue const& iss)
{
    path.emplaceBack(
        STPathElement::TypeCurrency | STPathElement::TypeIssuer,
        beast::kZero,
        iss.currency,
        iss.account);
    return *this;
}

inline TestPath&
TestPath::pushBack(MPTIssue const& iss)
{
    path.emplaceBack(
        STPathElement::TypeMpt | STPathElement::TypeIssuer,
        beast::kZero,
        iss.getMptID(),
        iss.getIssuer());
    return *this;
}

inline TestPath&
TestPath::pushBack(jtx::Account const& account)
{
    path.emplaceBack(account.id(), Currency{beast::kZero}, beast::kZero);
    return *this;
}

template <class First, class... Rest>
void
TestPath::addHelper(First&& first, Rest&&... rest)
{
    pushBack(std::forward<First>(first));
    if constexpr (sizeof...(rest) > 0)
        addHelper(std::forward<Rest>(rest)...);
}

inline json::Value
TestPath::json() const
{
    return path.getJson(JsonOptions::Values::None);
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
    [[nodiscard]] json::Value
    json() const
    {
        json::Value v;
        v["Paths"] = paths.getJson(JsonOptions::Values::None);
        return v;
    }

private:
    template <class First, class... Rest>
    void
    addHelper(First first, Rest... rest)
    {
        paths.emplaceBack(std::move(first.path));
        if constexpr (sizeof...(rest) > 0)
            addHelper(std::move(rest)...);
    }
};

}  // namespace xrpl::test
