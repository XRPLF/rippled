# Builds the Lean4 model library and links it into xrpld when formal_verification is on (default OFF).

if(NOT formal_verification)
    return()
endif()

if(NOT tests)
    message(FATAL_ERROR "formal_verification=ON requires tests=ON")
endif()

foreach(_var IN ITEMS LEAN4_BINDIR MATHLIB4_PACKAGES_DIR MATHLIB4_LAKE_MANIFEST)
    if(NOT ${_var})
        message(
            FATAL_ERROR
            "formal_verification=ON needs ${_var} from the Conan toolchain"
        )
    endif()
endforeach()

# Fail at configure time rather than as an opaque "cannot execute" mid-build.
execute_process(
    COMMAND ${LEAN4_BINDIR}/lean --githash
    RESULT_VARIABLE lean4_result
    OUTPUT_VARIABLE lean4_githash
    ERROR_VARIABLE lean4_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT lean4_result EQUAL 0)
    set(lean4_hint "")
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        string(
            CONCAT lean4_hint
            " The prebuilt lean4 package is built in the Nix CI environment "
            "and uses its loader: configure from the Nix development shell "
            "(nix develop .#formal-verification)."
        )
    endif()
    message(
        FATAL_ERROR
        "formal_verification: '${LEAN4_BINDIR}/lean' cannot be executed "
        "(${lean4_result}).${lean4_hint}\n${lean4_error}"
    )
endif()

# lake keys its build traces on LEAN_GITHASH when set (the dev shell sets it
# for its own lean): pin it to this toolchain's, so prebuilt mathlib matches.
set(lean4_lake
    ${CMAKE_COMMAND}
    -E
    env
    LEAN_GITHASH=${lean4_githash}
    ${LEAN4_BINDIR}/lake
)

# A lake workspace in the build dir, so the source tree is never written to.
# It links the sources and uses mathlib4's manifest and prebuilt packages.
set(lean4_src ${CMAKE_SOURCE_DIR}/formal_verification)
set(lean4_work ${CMAKE_BINARY_DIR}/formal_verification)
file(MAKE_DIRECTORY ${lean4_work}/.lake)
foreach(_link IN ITEMS XRPL XRPL.lean .lake/packages)
    file(REMOVE ${lean4_work}/${_link})
endforeach()
file(CREATE_LINK ${lean4_src}/XRPL ${lean4_work}/XRPL SYMBOLIC)
file(CREATE_LINK ${lean4_src}/XRPL.lean ${lean4_work}/XRPL.lean SYMBOLIC)
file(CREATE_LINK ${MATHLIB4_PACKAGES_DIR} ${lean4_work}/.lake/packages SYMBOLIC)
configure_file(
    ${MATHLIB4_LAKE_MANIFEST}
    ${lean4_work}/lake-manifest.json
    COPYONLY
)

file(
    GLOB_RECURSE lean4_model_sources
    CONFIGURE_DEPENDS
    ${lean4_src}/XRPL/Model/*.lean
    ${lean4_src}/XRPL/FFI/*.lean
)
set(lean4_model_archive ${lean4_work}/.lake/build/lib/libXRPL_XRPLModel.a)
add_custom_command(
    OUTPUT ${lean4_model_archive}
    COMMAND
        ${lean4_lake} -f ${lean4_src}/lakefile-model.toml build XRPLModel:static
    # lake leaves an up-to-date archive untouched; make must see it as new.
    COMMAND ${CMAKE_COMMAND} -E touch ${lean4_model_archive}
    WORKING_DIRECTORY ${lean4_work}
    DEPENDS ${lean4_model_sources} ${lean4_src}/lakefile-model.toml
    COMMENT "formal_verification: Lean4 model build"
    VERBATIM
)
add_custom_target(lean4_model DEPENDS ${lean4_model_archive})

# Checks every proof; --wfail, as lake only warns about `sorry`.
add_custom_target(
    lean4_proofs
    COMMAND ${lean4_lake} -f ${lean4_src}/lakefile.toml build --wfail
    WORKING_DIRECTORY ${lean4_work}
    COMMENT "formal_verification: Lean4 proof check"
    VERBATIM
)

# xrpld needs only the core Lean runtime (Init, leanrt): not lean4::lean4,
# which links libLake and the shared runtime. Static, as CI patches xrpld's
# rpath away and checks it has no dynamic libstdc++/libgcc dependency.
find_package(gmp REQUIRED)
find_package(libuv REQUIRED)
get_filename_component(lean4_root ${LEAN4_BINDIR} DIRECTORY)
add_dependencies(xrpld lean4_model)
target_include_directories(xrpld SYSTEM PRIVATE ${lean4_root}/include)
target_link_libraries(
    xrpld
    ${lean4_model_archive}
    ${lean4_root}/lib/lean/${CMAKE_STATIC_LIBRARY_PREFIX}Init${CMAKE_STATIC_LIBRARY_SUFFIX}
    ${lean4_root}/lib/lean/${CMAKE_STATIC_LIBRARY_PREFIX}leanrt${CMAKE_STATIC_LIBRARY_SUFFIX}
    gmp::libgmp
    libuv::uv_a
)
message(STATUS "formal_verification: Lean4 linked into xrpld")
