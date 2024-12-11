# file(CREATE_SYMLINK) only works on Windows with administrator privileges.
# https://stackoverflow.com/a/61244115/618906
function(create_symbolic_link target link)
  if(WIN32)
    if(NOT IS_SYMLINK "${link}")
      if(NOT IS_ABSOLUTE "${target}")
        # Relative links work do not work on Windows.
        set(target "${link}/../${target}")
      endif()
      file(TO_NATIVE_PATH "${target}" target)
      file(TO_NATIVE_PATH "${link}" link)
      execute_process(COMMAND cmd.exe /c mklink /J "${link}" "${target}")
    endif()
  else()
    file(CREATE_LINK "${target}" "${link}" SYMBOLIC)
  endif()
  if(NOT IS_SYMLINK "${link}")
    message(ERROR "failed to create symlink: <${link}>")
  endif()
endfunction()
