
# rippled Packaging and Containers

This folder contains docker container definitions and configuration
files to support building rpm and deb packages of rippled. The container
definitions include some additional software/packages that are used
for general build/test CI workflows of rippled but are not explicitly
needed for the package building workflow.

## CMake Targets

If you have docker installed on your local system, then the main 
CMake file will enable several targets related to building packages:
`rpm_container`, `rpm`, `dpkg_container`, and `dpkg`. The package targets
depend on the container targets and will trigger a build of those first.
The container builds can take several dozen minutes to complete (depending
on hardware specs), so quick build cycles are not possible currently. As
such, these targets are often best suited to CI/automated build systems.

The package build can be invoked like any other cmake target from the 
rippled root folder:
```
mkdir -p build/pkg && cd build/pkg
cmake -Dpackages_only=ON ../..
cmake --build . --target rpm
```
Upon successful completion, the generated package files will be in 
the `build/pkg/packages` directory. For deb packages, simply replace
`rpm` with `dpkg` in the build command above.


