## Building rippled with CLion IDE

I faced some issues in setting up the CLion IDE to work with the Conan build system. I'm hoping this document will alleviate some of those problems.
Try to build rippled code through your terminal to ensure that there are no dependency issues. Relevant instructions can be found [here](https://github.com/XRPLF/rippled/blob/develop/BUILD.md)

The following works in `CLion v2023.1.1`

### Reset the cache of CLion
Go to the root of rippled directory and execute the following commands to remove traces of previous compilation runs. 
1. `rm -rf .idea`
2. `rm -rf cmake-build-*`
3. `rm -rf cmake-release-*`
4. `rm -rf CMakeUserPresets.json`
5. CLion Toolbar -> File -> Invalidate Caches (click both options)
6. Remove the project from the CLion Quickstart menu


CLion can be configured to run rippled through updating the [CMake settings](https://www.jetbrains.com/help/clion/creating-new-project-from-scratch.html#open-prj) or by enabling a [Compilation Database](https://www.jetbrains.com/help/clion/compilation-database.html). But do not include both these alternatives at the same time.


### Configuring CMake settings
1. make sure the "Build type" field matches the one you passed to `conan install` command [here](https://github.com/XRPLF/rippled/blob/develop/BUILD.md#how-to-build-and-test)
2. make sure you pass `-DCMAKE_TOOLCHAIN_FILE` in the "CMake options" field just like the instructions say in the above webpage.
3. The `Toolchain` and `Generator` fields can be left with the defaults.  
Here is a sample image of my CMake settings ![image](https://github.com/XRPLF/rippled/assets/21219765/cecdcfff-cb9a-4682-8c90-e2ec2ea552d1)

### Using a Compilation Database
You can use the following command to generate a `compile_commands.json` file: 
`cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DCMAKE_TOOLCHAIN_FILE:FILEPATH=build/generators/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release ..`

Then, open the clion project with the compile_commands.json file. Make sure to go into `Tools -> Compilation Database -> Change Project Root` after import.



If CMake is configured correctly, you should be able to see a toolbar with green icons for build and run.![image](https://github.com/XRPLF/rippled/assets/21219765/f676af8e-f068-4fc7-a953-40b712a829d4)

