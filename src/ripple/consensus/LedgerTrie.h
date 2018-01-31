//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2017 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_APP_CONSENSUS_LEDGERS_TRIE_H_INCLUDED
#define RIPPLE_APP_CONSENSUS_LEDGERS_TRIE_H_INCLUDED

#include <algorithm>
#include <memory>
#include <vector>
#include <ripple/json/json_value.h>

namespace ripple {


/** Ancestry trie of ledgers

    A compressed trie tree that maintains validation support of recent ledgers
    based on their ancestry.

    The compressed trie structure comes from recognizing that ledger history
    can be viewed as a string over the alphabet of ledger ids. That is,
    a given ledger with sequence number `seq` defines a length `seq` string,
    with i-th entry equal to the id of the ancestor ledger with sequence
    number i. "Sequence" strings with a common prefix share those ancestor
    ledgers in common. Tracking this ancestry information and relations across
    all validated ledgers is done conveniently in a compressed trie. A node in
    the trie is an ancestor of all its children. If a parent node has sequence
    number `seq`, each child node has a different ledger starting at `seq+1`.
    The compression comes from the invariant that any non-root node with 0 tip
    support has either no children or multiple children. In other words, a
    non-root 0-tip-support node can be combined with its single child.

    Each node has a tipSupport, which is the number of current validations for
    that particular ledger. The node's branch support is the sum of the tip
    support and the branch support of that node's children:

        @code
        node->branchSupport = node->tipSupport;
        for (child : node->children)
           node->branchSupport += child->branchSupport;
        @endcode

    The templated Ledger type represents a ledger which has a unique history.
    It should be lightweight and cheap to copy.

       @code
       // Identifier types that should be equality-comparable and copyable
       struct ID;
       struct Seq;

       struct Ledger
       {
          struct MakeGenesis{};

          // The genesis ledger represents a ledger that prefixes all other
          // ledgers
          Ledger(MakeGenesis{});

          Ledger(Ledger const&);
          Ledger& operator=(Ledger const&);

          // Return the sequence number of this ledger
          Seq seq() const;

          // Return the ID of this ledger's ancestor with given sequence number
          // or ID{0} if unknown
          ID
          operator[](Seq s);

       };

       // Return the sequence number of the first possible mismatching ancestor
       // between two ledgers
       Seq
       mismatch(ledgerA, ledgerB);
       @endcode

    The unique history invariant of ledgers requires any ledgers that agree
    on the id of a given sequence number agree on ALL ancestors before that
    ledger:

        @code
        Ledger a,b;
        // For all Seq s:
        if(a[s] == b[s]);
            for(Seq p = 0; p < s; ++p)
                assert(a[p] == b[p]);
        @endcode

    @tparam Ledger A type representing a ledger and its history
*/
template <class Ledger>
class LedgerTrie
{
    using Seq = typename Ledger::Seq;
    using ID = typename Ledger::ID;

    /// Represents a span of ancestry of a ledger
    class Span
    {
        // The span is the half-open interval [start,end) of ledger_
        Seq start_{0};
        Seq end_{1};
        Ledger ledger_;

    public:
        Span() : ledger_{typename Ledger::MakeGenesis{}}
        {
            // Require default ledger to be genesis seq
            assert(ledger_.seq() == start_);
        }

        Span(Ledger ledger)
            : start_{0}, end_{ledger.seq() + Seq{1}}, ledger_{std::move(ledger)}
        {
        }

        Span(Span const& s) = default;
        Span(Span&& s) = default;
        Span&
        operator=(Span const&) = default;
        Span&
        operator=(Span&&) = default;

        Seq
        end() const
        {
            return end_;
        }

        // Return the Span from (spot,end_]
        Span
        from(Seq spot)
        {
            return sub(spot, end_);
        }

        // Return the Span from (start_,spot]
        Span
        before(Seq spot)
        {
            return sub(start_, spot);
        }

        bool
        empty() const
        {
            return start_ == end_;
        }

        //Return the ID of the ledger that starts this span
        ID
        startID() const
        {
            return ledger_[start_];
        }

        // Return the ledger sequence number of the first possible difference
        // between this span and a given ledger.
        Seq
        diff(Ledger const& o) const
        {
            return clamp(mismatch(ledger_, o));
        }

        //  The Seq and ID of the end of the span
        std::pair<Seq, ID>
        tip() const
        {
            Seq tipSeq{end_ - Seq{1}};
            return {tipSeq, ledger_[tipSeq]};
        }

    private:
        Span(Seq start, Seq end, Ledger const& l)
            : start_{start}, end_{end}, ledger_{l}
        {
            assert(start <= end);
        }

        Seq
        clamp(Seq val) const
        {
            return std::min(std::max(start_, val), end_);
        };

        // Return a span of this over the half-open interval [from,to)
        Span
        sub(Seq from, Seq to)
        {
            return Span(clamp(from), clamp(to), ledger_);
        }

        friend std::ostream&
        operator<<(std::ostream& o, Span const& s)
        {
            return o << s.tip().second << "[" << s.start_ << "," << s.end_
                     << ")";
        }

        friend Span
        merge(Span const& a, Span const& b)
        {
            // Return combined span, using ledger_ from higher sequence span
            if (a.end_ < b.end_)
                return Span(std::min(a.start_, b.start_), b.end_, b.ledger_);

            return Span(std::min(a.start_, b.start_), a.end_, a.ledger_);
        }
    };

    // A node in the trie
    struct Node
    {
        Node() = default;

        explicit Node(Ledger const& l) : span{l}, tipSupport{1}, branchSupport{1}
        {
        }

        explicit Node(Span s) : span{std::move(s)}
        {
        }

        Span span;
        std::uint32_t tipSupport = 0;
        std::uint32_t branchSupport = 0;

        std::vector<std::unique_ptr<Node>> children;
        Node* parent = nullptr;

        /** Remove the given node from this Node's children

            @param child The address of the child node to remove
            @note The child must be a member of the vector. The passed pointer
                  will be dangling as a result of this call
        */
        void
        erase(Node const* child)
        {
            auto it = std::find_if(
                children.begin(),
                children.end(),
                [child](std::unique_ptr<Node> const& curr) {
                    return curr.get() == child;
                });
            assert(it != children.end());
            using std::swap;
            swap(*it, children.back());
            children.pop_back();
        }

        friend std::ostream&
        operator<<(std::ostream& o, Node const& s)
        {
            return o << s.span << "(T:" << s.tipSupport
                     << ",B:" << s.branchSupport << ")";
        }

        Json::Value
        getJson() const
        {
            Json::Value res;
            res["id"] = to_string(span.tip().second);
            res["seq"] = static_cast<std::uint32_t>(span.tip().first);
            res["tipSupport"] = tipSupport;
            res["branchSupport"] = branchSupport;
            if(!children.empty())
            {
                Json::Value &cs = (res["children"] = Json::arrayValue);
                for (auto const& child : children)
                {
                    cs.append(child->getJson());
                }
            }
            return res;
        }
    };

    // The root of the trie. The root is allowed to break the no-single child
    // invariant.
    std::unique_ptr<Node> root;

    /** Find the node in the trie that represents the longest common ancestry
        with the given ledger.

        @return Pair of the found node and the sequence number of the first
                ledger difference.
    */
    std::pair<Node*, Seq>
    find(Ledger const& ledger) const
    {
        Node* curr = root.get();

        // Root is always defined and is in common with all ledgers
        assert(curr);
        Seq pos = curr->span.diff(ledger);

        bool done = false;

        // Continue searching for a better span as long as the current position
        // matches the entire span
        while (!done && pos == curr->span.end())
        {
            done = true;
            // Find the child with the longest ancestry match
            for (std::unique_ptr<Node> const& child : curr->children)
            {
                auto const childPos = child->span.diff(ledger);
                if (childPos > pos)
                {
                    done = false;
                    pos = childPos;
                    curr = child.get();
                    break;
                }
            }
        }
        return std::make_pair(curr, pos);
    }

    void
    dumpImpl(std::ostream& o, std::unique_ptr<Node> const& curr, int offset)
        const
    {
        if (curr)
        {
            if (offset > 0)
                o << std::setw(offset) << "|-";

            std::stringstream ss;
            ss << *curr;
            o << ss.str() << std::endl;
            for (std::unique_ptr<Node> const& child : curr->children)
                dumpImpl(o, child, offset + 1 + ss.str().size() + 2);
        }
    }

public:
    LedgerTrie() : root{std::make_unique<Node>()}
    {
    }

    /** Insert and/or increment the support for the given ledger.

        @param ledger A ledger and its ancestry
        @param count The count of support for this ledger
     */
    void
    insert(Ledger const& ledger, std::uint32_t count = 1)
    {
        Node* loc;
        Seq diffSeq;
        std::tie(loc, diffSeq) = find(ledger);

        // There is always a place to insert
        assert(loc);

        Span lTmp{ledger};
        Span prefix = lTmp.before(diffSeq);
        Span oldSuffix = loc->span.from(diffSeq);
        Span newSuffix = lTmp.from(diffSeq);
        Node* incNode = loc;

        if (!oldSuffix.empty())
        {
            // new is a prefix of current
            // e.g. abcdef->..., adding abcd
            //    becomes abcd->ef->...

            // Create oldSuffix node that takes over loc
            auto newNode = std::make_unique<Node>(oldSuffix);
            newNode->tipSupport = loc->tipSupport;
            newNode->branchSupport = loc->branchSupport;
            using std::swap;
            swap(newNode->children, loc->children);
            for(std::unique_ptr<Node> & child : newNode->children)
                child->parent = newNode.get();

            // Loc truncates to prefix and newNode is its child
            loc->span = prefix;
            newNode->parent = loc;
            loc->children.emplace_back(std::move(newNode));
            loc->tipSupport = 0;
        }
        if (!newSuffix.empty())
        {
            //  current is a substring of new
            // e.g.  abc->... adding abcde
            // ->   abc->  ...
            //          -> de

            auto newNode = std::make_unique<Node>(newSuffix);
            newNode->parent = loc;
            // increment support starting from the new node
            incNode = newNode.get();
            loc->children.push_back(std::move(newNode));
        }

        incNode->tipSupport += count;
        while (incNode)
        {
            incNode->branchSupport += count;
            incNode = incNode->parent;
        }
    }

    /** Decrease support for a ledger, removing and compressing if possible.

        @param ledger The ledger history to remove
        @param count The amount of tip support to remove

        @return Whether a matching node was decremented and possibly removed.
    */
    bool
    remove(Ledger const& ledger, std::uint32_t count = 1)
    {
        Node* loc;
        Seq diffSeq;
        std::tie(loc, diffSeq) = find(ledger);

        // Cannot erase root
        if (loc && loc != root.get())
        {
            // Must be exact match with tip support
            if (diffSeq == loc->span.end() && diffSeq > ledger.seq() &&
                loc->tipSupport > 0)
            {
                count = std::min(count, loc->tipSupport);
                loc->tipSupport -= count;

                Node* decNode = loc;
                while (decNode)
                {
                    decNode->branchSupport -= count;
                    decNode = decNode->parent;
                }

                while (loc->tipSupport == 0 && loc != root.get())
                {
                    Node* parent = loc->parent;
                    if (loc->children.empty())
                    {
                        // this node can be erased
                        parent->erase(loc);
                    }
                    else if (loc->children.size() == 1)
                    {
                        // This node can be combined with its child
                        std::unique_ptr<Node> child =
                            std::move(loc->children.front());
                        child->span = merge(loc->span, child->span);
                        child->parent = parent;
                        parent->children.emplace_back(std::move(child));
                        parent->erase(loc);
                    }
                    else
                        break;
                    loc = parent;
                }
                return true;
            }
        }
        return false;
    }

    /** Return count of tip support for the specific ledger.

        @param ledger The ledger to lookup
        @return The number of entries in the trie for this *exact* ledger
     */
    std::uint32_t
    tipSupport(Ledger const& ledger) const
    {
        Node const* loc;
        Seq diffSeq;
        std::tie(loc, diffSeq) = find(ledger);

        // Exact match
        if (loc && diffSeq == loc->span.end() && diffSeq > ledger.seq())
            return loc->tipSupport;
        return 0;
    }

    /** Return the count of branch support for the specific ledger

        @param ledger The ledger to lookup
        @return The number of entries in the trie for this ledger or a descendant
     */
    std::uint32_t
    branchSupport(Ledger const& ledger) const
    {
        Node const* loc;
        Seq diffSeq;
        std::tie(loc, diffSeq) = find(ledger);

        // Check that ledger is is an exact match or proper
        // prefix of loc
        if (loc && diffSeq > ledger.seq() &&
            ledger.seq() < loc->span.end())
        {
            return loc->branchSupport;
        }
        return 0;
    }

    /** Return the preferred ledger ID

        The preferred ledger is used to determine the working ledger
        for consensus amongst competing alternatives.

        Recall that each validator is normally validating a chain of ledgers,
        e.g. A->B->C->D. However, if due to network connectivity or other
        issues, validators generate different chains

        @code
               /->C
           A->B
               \->D->E
        @endcode

        we need a way for validators to converge on the chain with the most
        support. We call this the preferred ledger.  Intuitively, the idea is to
        be conservative and only switch to a different branch when you see
        enough peer validations to *know* another branch won't have preferred
        support. This ensures the preferred branch has monotonically increasing
        support.

        The preferred ledger is found by walking this tree of validated ledgers
        starting from the common ancestor ledger.

        At each sequence number, we have

           - The prior sequence preferred ledger, e.g. B.
           - The (tip) support of ledgers with this sequence number,e.g. the
             number of validators whose last validation was for C or D.
           - The (branch) total support of all descendants of the current
             sequence number ledgers, e.g. the branch support of D is the
             tip support of D plus the tip support of E; the branch support of
             C is just the tip support of C.
           - The number of validators that have yet to validate a ledger
             with this sequence number (prefixSupport).

        The preferred ledger for this sequence number is then the ledger
        with relative majority of support, where prefixSupport can be given to
        ANY ledger at that sequence number (including one not yet known). If no
        such preferred ledger exists, then prior sequence preferred ledger is
        the overall preferred ledger.

        In this example, for D to be preferred, the number of validators
        supporting it or a descendant must exceed the number of validators
        supporting C _plus_ the current prefix support. This is because if all
        the prefix support validators end up validating C, that new support must
        be less than that for D to be preferred.

        If a preferred ledger does exist, then we continue with the next
        sequence but increase prefixSupport with the non-preferred tip support
        this round, e.g. if C were preferred over D, then prefixSupport would
        increase by the support of D and E, since if those validators are
        following the protocol, they will switch to the C branch, but might
        initially support a different descendant.
    */
    std::pair<Seq,ID>
    getPreferred()
    {
        Node* curr = root.get();

        bool done = false;
        std::uint32_t prefixSupport = curr->tipSupport;
        while (curr && !done)
        {
            Node* best = nullptr;
            std::uint32_t margin = 0;

            if (curr->children.size() == 1)
            {
                best = curr->children[0].get();
                margin = best->branchSupport;
            }
            else if (!curr->children.empty())
            {
                // Sort placing children with largest branch support in the
                // front, breaking ties with the span's starting ID
                std::partial_sort(
                    curr->children.begin(),
                    curr->children.begin() + 2,
                    curr->children.end(),
                    [](std::unique_ptr<Node> const& a,
                       std::unique_ptr<Node> const& b) {
                        return std::make_tuple(a->branchSupport, a->span.startID()) >
                            std::make_tuple(b->branchSupport, b->span.startID());
                    });

                best = curr->children[0].get();
                margin = curr->children[0]->branchSupport -
                    curr->children[1]->branchSupport;

                // If best holds the tie-breaker, gets one larger margin
                // since the second best needs additional branchSupport
                // to overcome the tie
                if (best->span.startID() > curr->children[1]->span.startID())
                    margin++;
            }

            // If the best child has margin exceeding the prefix support,
            // continue from that child, otherwise we are done
            if (best && ((margin > prefixSupport) || (prefixSupport == 0)))
            {
                // Prefix support is all the support not on the branch we
                // are moving to
                //       curr
                //    _/  |  \_
                //    A   B  best
                // At curr, the prefix support already includes the tip support
                // of curr and its ancestors, along with the branch support of
                // any of its siblings that are inconsistent.
                //
                // The additional prefix support that is carried to best is
                //   A->branchSupport + B->branchSupport + best->tipSupport
                // This is the amount of support that has not yet voted
                // on a descendant of best, or has voted on a conflicting
                // descendant and will switch to best in the future. This means
                // that they may support an arbitrary descendant of best.
                //
                // The calculation is simplified using
                //     A->branchSupport+B->branchSupport
                //               =  curr->branchSupport - best->branchSupport
                //                                      - curr->tipSupport
                //
                // This will not overflow by definition of the above quantities
                prefixSupport += (curr->branchSupport - best->branchSupport
                                 - curr->tipSupport) + best->tipSupport;

                curr = best;
            }
            else  // current is the best
                done = true;
        }
        return curr->span.tip();
    }

    /** Dump an ascii representation of the trie to the stream
     */
    void
    dump(std::ostream& o) const
    {
        dumpImpl(o, root, 0);
    }

    /** Dump JSON representation of trie state
    */
    Json::Value
    getJson() const
    {
        return root->getJson();
    }

    /** Check the compressed trie and support invariants.
     */
    bool
    checkInvariants() const
    {
        std::stack<Node const*> nodes;
        nodes.push(root.get());
        while (!nodes.empty())
        {
            Node const* curr = nodes.top();
            nodes.pop();
            if (!curr)
                continue;

            // Node with 0 tip support must have multiple children
            // unless it is the root node
            if (curr != root.get() && curr->tipSupport == 0 &&
                curr->children.size() < 2)
                return false;

            // branchSupport = tipSupport + sum(child->branchSupport)
            std::size_t support = curr->tipSupport;
            for (auto const& child : curr->children)
            {
                if(child->parent != curr)
                    return false;

                support += child->branchSupport;
                nodes.push(child.get());
            }
            if (support != curr->branchSupport)
                return false;
        }
        return true;
    }
};

}  // namespace ripple
#endif
