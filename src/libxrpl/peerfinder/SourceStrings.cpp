#include <xrpl/peerfinder/detail/SourceStrings.h>

#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/peerfinder/detail/Source.h>

#include <memory>
#include <string>
#include <utility>

namespace xrpl::peer_finder {

class SourceStringsImp : public SourceStrings
{
public:
    SourceStringsImp(std::string name, Strings strings)
        : name_(std::move(name)), strings_(std::move(strings))
    {
    }

    ~SourceStringsImp() override = default;

    std::string const&
    name() override
    {
        return name_;
    }

    void
    fetch(Results& results, beast::Journal journal) override
    {
        results.addresses.resize(0);
        results.addresses.reserve(strings_.size());
        for (auto const& str : strings_)
        {
            beast::ip::Endpoint ep(beast::ip::Endpoint::fromString(str));
            if (isUnspecified(ep))
                ep = beast::ip::Endpoint::fromString(str);
            if (!isUnspecified(ep))
                results.addresses.push_back(ep);
        }
    }

private:
    std::string name_;
    Strings strings_;
};

//------------------------------------------------------------------------------

std::shared_ptr<Source>
SourceStrings::make(std::string const& name, Strings const& strings)
{
    return std::make_shared<SourceStringsImp>(name, strings);
}

}  // namespace xrpl::peer_finder
