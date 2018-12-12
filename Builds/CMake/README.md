
These are modules and sources that support our CMake build.

== FindBoost.cmake ==

In order to facilitate updating to latest releases of boost, we've made a local
copy of this cmake module in our repo. The latest official version can
generally be obtained
[here](https://github.com/Kitware/CMake/blob/master/Modules/FindBoost.cmake).

The latest version provided by Kitware can be tailored for use with the
version of CMake that it ships with (typically the next upcoming CMake
release). Our local version, however,  cannot necessarily assume a
specific version of CMake and might need to accommodate older versions,
depending on the min version we support for our project. As such, we need to
patch the stock FindBoost module. There is currently one patch file provided
here to accommodate CMake versions prior to 3.12 on windows. When updating to a
newer FindBoost, apply this patch via `patch < FindBoost.patch` after
downloading the latest `FindBoost.cmake` file. If the patch does not apply,
it's possible the applicable section of the original has changed: more
investigation will be required to resolve such a situation. Any backup files
created by `patch` can be deleted if/when the patch applies cleanly.
