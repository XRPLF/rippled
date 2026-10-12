#include <xrpl/protocol/BuildInfo.h>

#include <cstdio>

int
main(int argc, char const** argv)
{
    std::printf("%s\n", xrpl::build_info::getVersionString().c_str());
    return 0;
}
