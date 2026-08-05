#include <iostream>
#include <limits>

int
main()
{
    int maxInt = std::numeric_limits<int>::max();
    int volatile one = 1;
    std::cout << "Current max: " << maxInt << std::endl;
    int overflowed = maxInt + one;
    std::cout << "Overflowed result: " << overflowed << std::endl;
    return 0;
}
