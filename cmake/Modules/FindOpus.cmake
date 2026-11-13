#
# Try to find Opus libraries and include paths.
# Once done this will define
#
# Opus_FOUND
# OPUS_INCLUDE_DIR
# OPUS_LIBRARY
#

find_path(OPUS_INCLUDE_DIR opus/opus.h)

find_library(OPUS_LIBRARY NAMES opus)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Opus DEFAULT_MSG OPUS_LIBRARY OPUS_INCLUDE_DIR)

mark_as_advanced(OPUS_INCLUDE_DIR OPUS_LIBRARY)

if(Opus_FOUND AND NOT TARGET Opus::opus)
    add_library(Opus::opus IMPORTED UNKNOWN)
    set_target_properties(Opus::opus PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${OPUS_INCLUDE_DIR})
    if(OPUS_LIBRARY MATCHES "/([^/]+)\\.framework$")
        set_target_properties(Opus::opus PROPERTIES IMPORTED_LOCATION ${OPUS_LIBRARY}/${CMAKE_MATCH_1})
    else()
        set_target_properties(Opus::opus PROPERTIES IMPORTED_LOCATION ${OPUS_LIBRARY})
    endif()
endif()
