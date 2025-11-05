#include <xrpld/peerfinder/detail/SourceStrings.h>

namespace ripple {
namespace PeerFinder {

class SourceStringsImp : public SourceStrings
{
public:
    SourceStringsImp(std::string const& name, Strings const& strings)
        : m_name(name), m_strings(strings)
    {
    }

    ~SourceStringsImp() = default;

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
            beast::IP::Endpoint ep(
                beast::IP::Endpoint::from_string(m_strings[i]));
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

}  // namespace PeerFinder
}  // namespace ripple
