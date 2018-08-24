# This patches unsigned-types.h in the soci official sources
# so as to remove type range check exceptions that cause
# us trouble when using boost::optional to select int values
file (STRINGS include/soci/unsigned-types.h sourcecode)
foreach (line_ ${sourcecode})
  if (line_ MATCHES "^[ \\t]+throw[ ]+soci_error[ ]*\\([ ]*\"Value outside of allowed.+$")
    set (line_ "//${CMAKE_MATCH_0}")
  endif ()
  file (APPEND include/soci/unsigned-types.h.patched "${line_}\n")
endforeach ()
file (RENAME include/soci/unsigned-types.h include/soci/unsigned-types.h.orig)
file (RENAME include/soci/unsigned-types.h.patched include/soci/unsigned-types.h)

