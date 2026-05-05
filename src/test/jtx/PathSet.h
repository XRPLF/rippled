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
    forEachItem(*env.current(), account, [&](std::shared_ptr<SLE const> const& sle) {
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
isOffer(jtx::Env& env, jtx::Account const& account, Asset const& takerPays, Asset const& takerGets)
{
    return countOffers(env, account, takerPays, takerGets) > 0;
}

class Path
{
public:
    STPath path;

    Path() = default;
    Path(Path const&) = default;
    Path&
    operator=(Path const&) = default;
    Path(Path&&) = default;
    Path&
    operator=(Path&&) = default;

    template <class First, class... Rest>
    explicit Path(First&& first, Rest&&... rest)
    {
        addHelper(std::forward<First>(first), std::forward<Rest>(rest)...);
    }
    Path&
    pushBack(Issue const& iss);
    Path&
    pushBack(MPTIssue const& iss);
    Path&
    pushBack(jtx::Account const& acc);
    Path&
    pushBack(STPathElement const& pe);
    [[nodiscard]] Json::Value
    json() const;

private:
    template <class First, class... Rest>
    void
    addHelper(First&& first, Rest&&... rest);
};

inline Path&
Path::pushBack(STPathElement const& pe)
{
    path.emplaceBack(pe);
    return *this;
}

inline Path&
Path::pushBack(Issue const& iss)
{
    path.emplaceBack(
        STPathElement::TypeCurrency | STPathElement::TypeIssuer,
        beast::kZERO,
        iss.currency,
        iss.account);
    return *this;
}

inline Path&
Path::pushBack(MPTIssue const& iss)
{
    path.emplaceBack(
        STPathElement::TypeMpt | STPathElement::TypeIssuer,
        beast::kZERO,
        iss.getMptID(),
        iss.getIssuer());
    return *this;
}

inline Path&
Path::pushBack(jtx::Account const& account)
{
    path.emplaceBack(account.id(), Currency{beast::kZERO}, beast::kZERO);
    return *this;
}

template <class First, class... Rest>
void
Path::addHelper(First&& first, Rest&&... rest)
{
    pushBack(std::forward<First>(first));
    if constexpr (sizeof...(rest) > 0)
        addHelper(std::forward<Rest>(rest)...);
}

inline Json::Value
Path::json() const
{
    return path.getJson(JsonOptions::None);
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
    [[nodiscard]] Json::Value
    json() const
    {
        Json::Value v;
        v["Paths"] = paths.getJson(JsonOptions::None);
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
