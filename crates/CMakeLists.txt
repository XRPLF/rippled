find_package(Corrosion REQUIRED)

corrosion_import_crate(MANIFEST_PATH ${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml)

# The generated C++ lands in the build tree, so put a .clang-tidy next to it to
# keep clang-tidy from analyzing code we don't own.
configure_file(
    generated.clang-tidy
    "${CMAKE_CURRENT_BINARY_DIR}/.clang-tidy"
    COPYONLY
)

add_custom_target(xrpl_crates)
add_dependencies(tidy_prerequisites xrpl_crates)

function(_unlink_libgcc_s crate)
    if(NOT (is_linux AND static))
        return()
    endif()

    # Corrosion exposes a crate's staticlib as an imported `<crate>-static`
    # target and puts the native libs in its INTERFACE_LINK_LIBRARIES. If either
    # of those changes, warn instead of silently letting libgcc_s.so.1 return.
    set(imported "${crate}-static")
    if(NOT TARGET ${imported})
        message(
            FATAL_ERROR
            "Corrosion did not create the imported target '${imported}', so "
            "libgcc_s cannot be removed from the link interface of '${crate}'. "
            "xrpld will link libgcc_s.so.1 dynamically. Check where Corrosion "
            "${CORROSION_VERSION} now records `native-static-libs`."
        )
        return()
    endif()

    get_target_property(libs ${imported} INTERFACE_LINK_LIBRARIES)
    if(NOT "gcc_s" IN_LIST libs)
        message(
            FATAL_ERROR
            "'gcc_s' was not in the link interface of '${imported}' as "
            "expected. If the Rust toolchain stopped reporting it this "
            "workaround is obsolete and can be deleted; otherwise xrpld may "
            "link libgcc_s.so.1 dynamically. Verify with: "
            "objdump -p xrpld | grep NEEDED"
        )
        return()
    endif()

    list(REMOVE_ITEM libs gcc_s)
    set_property(TARGET ${imported} PROPERTY INTERFACE_LINK_LIBRARIES ${libs})
endfunction()

function(add_xrpl_crate name)
    cmake_parse_arguments(ARG "" "CRATE" "FILES" ${ARGN})
    _unlink_libgcc_s(${ARG_CRATE})
    corrosion_add_cxxbridge(${name}_cxxbridge CRATE ${ARG_CRATE} FILES
                            ${ARG_FILES}
    )
    # Generated cxxbridge headers don't exist at configure time; CMake 3.28+
    # validates INTERFACE_SOURCES on consuming targets. Clear it to skip the
    # existence check — build-time ordering is enforced by the custom commands.
    set_target_properties(${name}_cxxbridge PROPERTIES INTERFACE_SOURCES "")
    add_dependencies(xrpl_crates ${name}_cxxbridge)
endfunction()

add_xrpl_crate(rs_hello_world CRATE rs_hello_world FILES lib.rs)
