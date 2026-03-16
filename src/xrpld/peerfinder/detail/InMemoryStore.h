#pragma once

#include <xrpld/peerfinder/detail/Store.h>

namespace xrpl {
namespace PeerFinder {

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

}  // namespace PeerFinder
}  // namespace xrpl
