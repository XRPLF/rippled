
These are modules and sources that support our CMake build.

== FindBoost.cmake ==

In order to facilitate updating to latest releases of boost, we've made a local
copy of the FindBoost cmake module in our repo. The latest official version can
generally be obtained
[here](https://github.com/Kitware/CMake/blob/master/Modules/FindBoost.cmake).

The latest version provided by Kitware can be tailored for use with the
version of CMake that it ships with (typically the next upcoming CMake
release). As such, the latest version from the repository might not work
perfectly with older versions of CMake - for instance, the latest version 
might use features or properties only available in the version of CMake that 
it ships with. Given this, it's best to test any updates to this module with a few 
different versions of cmake.

