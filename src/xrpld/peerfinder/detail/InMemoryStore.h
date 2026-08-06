#pragma once

#include <xrpl/peerfinder/detail/Store.h>

#include <vector>

namespace xrpl::peer_finder {

/**
 * In-memory PeerFinder store used when relational_db backend is RWDB.
 */
class InMemoryStore : public Store
{
private:
    std::vector<Entry> entries_;

public:
    std::size_t
    load(load_callback const& cb) override
    {
        for (auto const& entry : entries_)
            cb(entry.endpoint, entry.valence);

        return entries_.size();
    }

    void
    save(std::vector<Entry> const& v) override
    {
        entries_ = v;
    }
};

}  // namespace xrpl::peer_finder
