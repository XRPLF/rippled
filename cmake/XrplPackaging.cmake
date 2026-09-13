#[===================================================================[
   Linux packaging support: 'package' target.

   The packaging script (package/build_pkg.py) installs to FHS-standard
   paths (/usr/bin, /etc/xrpld, etc.) regardless of CMAKE_INSTALL_PREFIX,
   so no prefix guard is needed here.
#]===================================================================]
if(NOT is_linux)
    message(STATUS "Packaging not supported on non-Linux hosts")
    return()
endif()

if(NOT DEFINED pkg_release)
    set(pkg_release 1)
endif()

find_program(RPMBUILD_EXECUTABLE rpmbuild)
find_program(DPKG_BUILDPACKAGE_EXECUTABLE dpkg-buildpackage)

if(NOT (RPMBUILD_EXECUTABLE OR DPKG_BUILDPACKAGE_EXECUTABLE))
    message(
        STATUS
        "Neither rpmbuild nor dpkg-buildpackage found; 'package' target not available"
    )
    return()
endif()

if(NOT TARGET xrpld)
    message(STATUS "xrpld=ON is required; 'package' target not available")
    return()
endif()

if(NOT TARGET validator-keys)
    message(
        STATUS
        "validator_keys=ON is required; 'package' target not available"
    )
    return()
endif()

if(DPKG_BUILDPACKAGE_EXECUTABLE)
    set(pkg_type deb)
else()
    set(pkg_type rpm)
endif()

add_custom_target(
    package
    COMMAND
        ${CMAKE_SOURCE_DIR}/package/build_pkg.py --package-type=${pkg_type}
        --build-dir=${CMAKE_BINARY_DIR} --pkg-release=${pkg_release}
        --channel=UNRELEASED
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS xrpld validator-keys
    COMMENT "Building Linux ${pkg_type} package"
    VERBATIM
)
