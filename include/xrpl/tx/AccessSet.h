#pragma once

#include <xrpl/basics/base_uint.h>

#include <boost/container/flat_set.hpp>

#include <set>

namespace xrpl {

/** The statically-declared ledger footprint of a single transaction.

    An AccessSet enumerates the ledger entries a transaction may read or write,
    grouped by category for readability. It is produced by `accessSetOf()` from
    the signed transaction body (and, where required, a read-only snapshot of
    the closed ledger) — never from another transaction's in-flight writes.

    Two transactions are independent (safe to apply concurrently) iff their
    AccessSets do not conflict; see `conflictsWith()`. A transaction whose
    footprint cannot be enumerated statically sets `touchesGlobal` and is
    serialized against everything.

    Directory-node (ltDIR_NODE) entries are intentionally NOT represented here.
    Owner-directory bookkeeping is a consequence of mutating the owning account,
    whose AccountRoot is already declared; shared (book) directories belong only
    to transactors that are `touchesGlobal` in this version. Conflict detection
    on directories is therefore subsumed by the account/object entries.

    The category split is for human readability and future scheduler heuristics;
    conflict detection itself operates on the flat union, `keys()`.
*/
struct AccessSet
{
    boost::container::flat_set<uint256> accounts;     // AccountRoot entries
    boost::container::flat_set<uint256> trustlines;   // RippleState entries
    boost::container::flat_set<uint256> offerBooks;   // book directory roots
    boost::container::flat_set<uint256> ammPools;     // AMM root entries
    boost::container::flat_set<uint256> nftPages;     // NFToken page roots
    boost::container::flat_set<uint256> miscObjects;  // escrow/check/ticket/etc.

    /** When true, the footprint is not statically known; serialize this tx. */
    bool touchesGlobal = false;

    /** The conservative "I touch everything" footprint. */
    [[nodiscard]] static AccessSet
    global()
    {
        AccessSet a;
        a.touchesGlobal = true;
        return a;
    }

    /** The flat union of every declared entry key, across all categories. */
    [[nodiscard]] std::set<uint256>
    keys() const
    {
        std::set<uint256> out;
        for (auto const* s :
             {&accounts, &trustlines, &offerBooks, &ammPools, &nftPages, &miscObjects})
            out.insert(s->begin(), s->end());
        return out;
    }

    /** True if these two transactions cannot safely apply concurrently.

        Either side touching global state forces a conflict; otherwise the two
        conflict iff their declared key sets intersect.
    */
    [[nodiscard]] bool
    conflictsWith(AccessSet const& other) const
    {
        if (touchesGlobal || other.touchesGlobal)
            return true;

        auto const a = keys();
        auto const b = other.keys();
        auto i = a.begin();
        auto j = b.begin();
        while (i != a.end() && j != b.end())
        {
            if (*i < *j)
                ++i;
            else if (*j < *i)
                ++j;
            else
                return true;
        }
        return false;
    }
};

}  // namespace xrpl
