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

#include <ripple/json/json_value.h>
#include <boost/optional.hpp>
#include <algorithm>
#include <memory>
#include <vector>

namespace ripple {

/** The tip of a span of ledger ancestry
 */
template <class Ledger>
class SpanTip
{
public:
    using Seq = typename Ledger::Seq;
    using ID = typename Ledger::ID;

    SpanTip(Seq s, ID i, Ledger const lgr)
        : seq{s}, id{i}, ledger{std::move(lgr)}
    {
    }

    // The sequence number of the tip ledger
    Seq seq;
    // The ID of the tip ledger
    ID id;

    /** Lookup the ID of an ancestor of the tip ledger

        @param s The sequence number of the ancestor
        @return The ID of the ancestor with that sequence number

        @note s must be less than or equal to the sequence number of the
              tip ledger
    */
    ID
    ancestor(Seq const& s) const
    {
        assert(s <= seq);
        return ledger[s];
    }

private:
    Ledger const ledger;
};

namespace ledger_trie_detail {

// Represents a span of ancestry of a ledger
template <class Ledger>
class Span
{
    using Seq = typename Ledger::Seq;
    using ID = typename Ledger::ID;

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
    start() const
    {
        return start_;
    }

    Seq
    end() const
    {
        return end_;
    }

    // Return the Span from [spot,end_) or none if no such valid span
    boost::optional<Span>
    from(Seq spot) const
    {
        return sub(spot, end_);
    }

    // Return the Span from [start_,spot) or none if no such valid span
    boost::optional<Span>
    before(Seq spot) const
    {
        return sub(start_, spot);
    }

    // Return the ID of the ledger that starts this span
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

    //  The tip of this span
    SpanTip<Ledger>
    tip() const
    {
        Seq tipSeq{end_ - Seq{1}};
        return SpanTip<Ledger>{tipSeq, ledger_[tipSeq], ledger_};
    }

private:
    Span(Seq start, Seq end, Ledger const& l)
        : start_{start}, end_{end}, ledger_{l}
    {
        // Spans cannot be empty
        assert(start < end);
    }

    Seq
    clamp(Seq val) const
    {
        return std::min(std::max(start_, val), end_);
    }

    // Return a span of this over the half-open interval [from,to)
    boost::optional<Span>
    sub(Seq from, Seq to) const
    {
        Seq newFrom = clamp(from);
        Seq newTo = clamp(to);
        if (newFrom < newTo)
            return Span(newFrom, newTo, ledger_);
        return boost::none;
    }

    friend std::ostream&
    operator<<(std::ostream& o, Span const& s)
    {
        return o << s.tip().id << "[" << s.start_ << "," << s.end_ << ")";
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
template <class Ledger>
struct Node
{
    Node() = default;

    explicit Node(Ledger const& l) : span{l}, tipSupport{1}, branchSupport{1}
    {
    }

    explicit Node(Span<Ledger> s) : span{std::move(s)}
    {
    }

    Span<Ledger> span;
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
        std::swap(*it, children.back());
        children.pop_back();
    }

    friend std::ostream&
    operator<<(std::ostream& o, Node const& s)
    {
        return o << s.span << "(T:" << s.tipSupport << ",B:" << s.branchSupport
                 << ")";
    }

    Json::Value
    getJson() const
    {
        Json::Value res;
        res["id"] = to_string(span.tip().id);
        res["seq"] = static_cast<std::uint32_t>(span.tip().seq);
        res["tipSupport"] = tipSupport;
        res["branchSupport"] = branchSupport;
        if (!children.empty())
        {
            Json::Value& cs = (res["children"] = Json::arrayValue);
            for (auto const& child : children)
            {
                cs.append(child->getJson());
            }
        }
        return res;
    }
};
}  // namespace ledger_trie_detail

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

    using Node = ledger_trie_detail::Node<Ledger>;
    using Span = ledger_trie_detail::Span<Ledger>;

    // The root of the trie. The root is allowed to break the no-single child
    // invariant.
    std::unique_ptr<Node> root;

    // Count of the tip support for each sequence number
    std::map<Seq, std::uint32_t> seqSupport;

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

        // Node from which to start incrementing branchSupport
        Node* incNode = loc;

        // loc->span has the longest common prefix with Span{ledger} of all
        // existing nodes in the trie. The optional<Span>'s below represent
        // the possible common suffixes between loc->span and Span{ledger}.
        //
        // loc->span
        //  a b c  | d e f
        //  prefix | oldSuffix
        //
        // Span{ledger}
        //  a b c  | g h i
        //  prefix | newSuffix

        boost::optional<Span> prefix = loc->span.before(diffSeq);
        boost::optional<Span> oldSuffix = loc->span.from(diffSeq);
        boost::optional<Span> newSuffix = Span{ledger}.from(diffSeq);

        if (oldSuffix)
        {
            // Have
            //   abcdef -> ....
            // Inserting
            //   abc
            // Becomes
            //   abc -> def -> ...

            // Create oldSuffix node that takes over loc
            auto newNode = std::make_unique<Node>(*oldSuffix);
            newNode->tipSupport = loc->tipSupport;
            newNode->branchSupport = loc->branchSupport;
            newNode->children = std::move(loc->children);
            assert(loc->children.empty());
            for (std::unique_ptr<Node>& child : newNode->children)
                child->parent = newNode.get();

            // Loc truncates to prefix and newNode is its child
            assert(prefix);
            loc->span = *prefix;
            newNode->parent = loc;
            loc->children.emplace_back(std::move(newNode));
            loc->tipSupport = 0;
        }
        if (newSuffix)
        {
            // Have
            //  abc -> ...
            // Inserting
            //  abcdef-> ...
            // Becomes
            //  abc -> ...
            //     \-> def

            auto newNode = std::make_unique<Node>(*newSuffix);
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

        seqSupport[ledger.seq()] += count;
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

        if (loc)
        {
            // Must be exact match with tip support
            if (diffSeq == loc->span.end() && diffSeq > ledger.seq() &&
                loc->tipSupport > 0)
            {
                count = std::min(count, loc->tipSupport);
                loc->tipSupport -= count;

                auto const it = seqSupport.find(ledger.seq());
                assert(it != seqSupport.end() && it->second >= count);
                it->second -= count;
                if (it->second == 0)
                    seqSupport.erase(it->first);

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
        @return The number of entries in the trie for this ledger or a
                descendant
     */
    std::uint32_t
    branchSupport(Ledger const& ledger) const
    {
        Node const* loc;
        Seq diffSeq;
        std::tie(loc, diffSeq) = find(ledger);

        // Check that ledger is is an exact match or proper
        // prefix of loc
        if (loc && diffSeq > ledger.seq() && ledger.seq() < loc->span.end())
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
             with this sequence number (uncommitted support). Uncommitted
             includes all validators whose last sequence number is smaller than
             our last issued sequence number, since due to asynchrony, we may
             not have heard from those nodes yet.

        The preferred ledger for this sequence number is then the ledger
        with relative majority of support, where uncommitted support
        can be given to ANY ledger at that sequence number
        (including one not yet known). If no such preferred ledger exists, then
        the prior sequence preferred ledger is the overall preferred ledger.

        In this example, for D to be preferred, the number of validators
        supporting it or a descendant must exceed the number of validators
        supporting C _plus_ the current uncommitted support. This is because if
        all uncommitted validators end up validating C, that new support must
        be less than that for D to be preferred.

        If a preferred ledger does exist, then we continue with the next
        sequence using that ledger as the root.

        @param largestIssued The sequence number of the largest validation
                             issued by this node.
        @return Pair with the sequence number and ID of the preferred ledger or
                boost::none if no preferred ledger exists
    */
    boost::optional<SpanTip<Ledger>>
    getPreferred(Seq const largestIssued) const
    {
        if (empty())
            return boost::none;

        Node* curr = root.get();

        bool done = false;

        std::uint32_t uncommitted = 0;
        auto uncommittedIt = seqSupport.begin();

        while (curr && !done)
        {
            // Within a single span, the preferred by branch strategy is simply
            // to continue along the span as long as the branch support of
            // the next ledger exceeds the uncommitted support for that ledger.
            {
                // Add any initial uncommitted support prior for ledgers
                // earlier than nextSeq or earlier than largestIssued
                Seq nextSeq = curr->span.start() + Seq{1};
                while (uncommittedIt != seqSupport.end() &&
                       uncommittedIt->first < std::max(nextSeq, largestIssued))
                {
                    uncommitted += uncommittedIt->second;
                    uncommittedIt++;
                }

                // Advance nextSeq along the span
                while (nextSeq < curr->span.end() &&
                       curr->branchSupport > uncommitted)
                {
                    // Jump to the next seqSupport change
                    if (uncommittedIt != seqSupport.end() &&
                        uncommittedIt->first < curr->span.end())
                    {
                        nextSeq = uncommittedIt->first + Seq{1};
                        uncommitted += uncommittedIt->second;
                        uncommittedIt++;
                    }
                    else  // otherwise we jump to the end of the span
                        nextSeq = curr->span.end();
                }
                // We did not consume the entire span, so we have found the
                // preferred ledger
                if (nextSeq < curr->span.end())
                    return curr->span.before(nextSeq)->tip();
            }

            // We have reached the end of the current span, so we need to
            // find the best child
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
                        return std::make_tuple(
                                   a->branchSupport, a->span.startID()) >
                            std::make_tuple(
                                   b->branchSupport, b->span.startID());
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

            // If the best child has margin exceeding the uncommitted support,
            // continue from that child, otherwise we are done
            if (best && ((margin > uncommitted) || (uncommitted == 0)))
                curr = best;
            else  // current is the best
                done = true;
        }
        return curr->span.tip();
    }

    /** Return whether the trie is tracking any ledgers
     */
    bool
    empty() const
    {
        return !root || root->branchSupport == 0;
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
        std::map<Seq, std::uint32_t> expectedSeqSupport;

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
            if (curr->tipSupport != 0)
                expectedSeqSupport[curr->span.end() - Seq{1}] +=
                    curr->tipSupport;

            for (auto const& child : curr->children)
            {
                if (child->parent != curr)
                    return false;

                support += child->branchSupport;
                nodes.push(child.get());
            }
            if (support != curr->branchSupport)
                return false;
        }
        return expectedSeqSupport == seqSupport;
    }
};

}  // namespace ripple
#endif
