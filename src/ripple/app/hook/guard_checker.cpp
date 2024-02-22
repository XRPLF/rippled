#include <stdio.h>
#include <stdint.h>
#include <vector>
#include <string_view>
#include <optional>
#include "Guard.h"
#include <iostream>
#include <ostream>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

int main(int argc, char** argv)
{
    if (argc != 2)
        return fprintf(stderr, "Guard Checker\n\tUsage: %s somefile.wasm\n", argv[0]);
    

    int fd = open(argv[1], O_RDONLY);
    if (fd < 0)
        return fprintf(stderr, "Could not open file for reading:`%s`\n", argv[1]);

    size_t len = lseek(fd, 0, SEEK_END);

    lseek(fd, 0, SEEK_SET);

    std::vector<uint8_t> hook(len);

    uint8_t* ptr = hook.data();

    size_t upto = 0;
    while (upto < len)
    {
        size_t bytes_read = read(fd, ptr + upto, len - upto);
        if (bytes_read < 0)
            return fprintf(stderr, "Error reading file `%s`, only %ld bytes could be read\n", argv[1], upto);
        upto += bytes_read;
    }

    printf("Read %ld bytes from `%s` successfully...\n", upto, argv[1]);

    close(fd);

    auto result = 
        validateGuards(hook, true, std::cout, "");

    if (!result)
    {
        printf("Hook validation failed.\n");
        return 1;
    }

    printf("\nHook validation successful!\n");

    return 0;
}
