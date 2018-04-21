
## "target" parsing..DEPRECATED and will be removed in future
macro(parse_target)
  if (target)
    # Parse the target
    set(remaining ${target})
    while (remaining)
      # get the component up to the next dot or end
      string(REGEX REPLACE "^\\.?([^\\.]+).*$" "\\1" cur_component ${remaining})
      string(REGEX REPLACE "^\\.?[^\\.]+(.*$)" "\\1" remaining ${remaining})

      if (${cur_component} STREQUAL gcc)
        if (DEFINED ENV{GNU_CC})
          set(CMAKE_C_COMPILER $ENV{GNU_CC})
        elseif ($ENV{CC} MATCHES .*gcc.*)
          set(CMAKE_C_COMPILER $ENV{CC})
        else()
          find_program(CMAKE_C_COMPILER gcc)
        endif()

        if (DEFINED ENV{GNU_CXX})
          set(CMAKE_CXX_COMPILER $ENV{GNU_CXX})
        elseif ($ENV{CXX} MATCHES .*g\\+\\+.*)
          set(CMAKE_CXX_COMPILER $ENV{CXX})
        else()
          find_program(CMAKE_CXX_COMPILER g++)
        endif()
      endif()

      if (${cur_component} STREQUAL clang)
        if (DEFINED ENV{CLANG_CC})
          set(CMAKE_C_COMPILER $ENV{CLANG_CC})
        elseif ($ENV{CC} MATCHES .*clang.*)
          set(CMAKE_C_COMPILER $ENV{CC})
        else()
          find_program(CMAKE_C_COMPILER clang)
        endif()

        if (DEFINED ENV{CLANG_CXX})
          set(CMAKE_CXX_COMPILER $ENV{CLANG_CXX})
        elseif ($ENV{CXX} MATCHES .*clang.*)
          set(CMAKE_CXX_COMPILER $ENV{CXX})
        else()
          find_program(CMAKE_CXX_COMPILER clang++)
        endif()
      endif()

      if (${cur_component} STREQUAL msvc)
        # TBD
      endif()

      if (${cur_component} STREQUAL unity)
        set(unity true)
      endif()

      if (${cur_component} STREQUAL nounity)
        set(unity false)
      endif()

      if (${cur_component} STREQUAL debug)
        set(release false)
      endif()

      if (${cur_component} STREQUAL release)
        set(release true)
      endif()

      if (${cur_component} STREQUAL coverage)
        set(coverage true)
        set(debug true)
      endif()

      if (${cur_component} STREQUAL profile)
        set(profile true)
      endif()
    endwhile()
  endif()

  if(CMAKE_C_COMPILER MATCHES "-NOTFOUND$" OR
    CMAKE_CXX_COMPILER MATCHES "-NOTFOUND$")
    message(FATAL_ERROR "Can not find appropriate compiler for target ${target}")
  endif()

  if (release)
    set(CMAKE_BUILD_TYPE Release)
  else()
    set(CMAKE_BUILD_TYPE Debug)
  endif()
endmacro()

############################################################

macro(group_sources_in source_dir curdir)
  file(GLOB children RELATIVE ${source_dir}/${curdir}
    ${source_dir}/${curdir}/*)
  foreach (child ${children})
    if (IS_DIRECTORY ${source_dir}/${curdir}/${child})
      group_sources_in(${source_dir} ${curdir}/${child})
    else()
      string(REPLACE "/" "\\" groupname ${curdir})
      source_group(${groupname} FILES
        ${source_dir}/${curdir}/${child})
    endif()
  endforeach()
endmacro()

macro(group_sources curdir)
  group_sources_in(${PROJECT_SOURCE_DIR} ${curdir})
endmacro()

