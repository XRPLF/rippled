#include <cstdio>

#include <xrpl/protocol/BuildInfo.h>

int main(int argc, const char** argv) {
    std::printf("%s\n", ripple::BuildInfo::getVersionString().c_str());
    return 0;
}
