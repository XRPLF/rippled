#include <boost/optional.hpp>

#include <vector>

namespace boost {
namespace filesystem {
class path;
}
}  // namespace boost

std::string const&
getVersionString();

void
createKeyFile(boost::filesystem::path const& keyFile);

void
createToken(boost::filesystem::path const& keyFile);

void
createRevocation(boost::filesystem::path const& keyFile);

void
signData(std::string const& data, boost::filesystem::path const& keyFile);

int
runCommand(
    std::string const& command,
    std::vector<std::string> const& arg,
    boost::filesystem::path const& keyFile);
