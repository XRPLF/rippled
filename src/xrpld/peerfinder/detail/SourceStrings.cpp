#include <xrpld/peerfinder/detail/SourceStrings.h>

#include <xrpld/peerfinder/detail/Source.h>

#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/Journal.h>

#include <memory>
#include <string>
#include <utility>

namespace xrpl::PeerFinder {

class SourceStringsImp : public SourceStrings
{
public:
    SourceStringsImp(std::string name, Strings strings)
        : m_name(std::move(name)), m_strings(std::move(strings))
    {
    }

    ~SourceStringsImp() override = default;

    std::string const&
    name() override
    {
        return m_name;
    }

    void
    fetch(Results& results, beast::Journal journal) override
    {
        results.addresses.resize(0);
        results.addresses.reserve(m_strings.size());
        for (int i = 0; i < m_strings.size(); ++i)
        {
            beast::IP::Endpoint ep(beast::IP::Endpoint::from_string(m_strings[i]));
            if (is_unspecified(ep))
                ep = beast::IP::Endpoint::from_string(m_strings[i]);
            if (!is_unspecified(ep))
                results.addresses.push_back(ep);
        }
    }

private:
    std::string m_name;
    Strings m_strings;
};

//------------------------------------------------------------------------------

std::shared_ptr<Source>
SourceStrings::New(std::string const& name, Strings const& strings)
{
    return std::make_shared<SourceStringsImp>(name, strings);
}

}  // namespace xrpl::PeerFinder
