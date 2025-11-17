#ifndef XRPL_PEERFINDER_STORE_H_INCLUDED
#define XRPL_PEERFINDER_STORE_H_INCLUDED

namespace ripple {
namespace PeerFinder {

/** Abstract persistence for PeerFinder data. */
class Store
{
public:
    virtual ~Store()
    {
    }

    // load the bootstrap cache
    using load_callback = std::function<void(beast::IP::Endpoint, int)>;
    virtual std::size_t
    load(load_callback const& cb) = 0;

    // save the bootstrap cache
    struct Entry
    {
        explicit Entry() = default;

        beast::IP::Endpoint endpoint;
        int valence;
    };
    virtual void
    save(std::vector<Entry> const& v) = 0;
};

}  // namespace PeerFinder
}  // namespace ripple

#endif
