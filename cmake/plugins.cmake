include(FetchContent)

function(ecewo_define_plugin NAME)
  set(oneValueArgs REPO DEFAULT_VERSION)
  cmake_parse_arguments(P "" "${oneValueArgs}" "" ${ARGN})

  if(NOT P_REPO)
    message(FATAL_ERROR
    "ecewo_define_plugin(${NAME}) requires REPO")
  endif()

  set(ECEWO_PLUGIN_${NAME}_REPO
    ${P_REPO}
    CACHE INTERNAL "ecewo plugin ${NAME} repo"
  )

  if(P_DEFAULT_VERSION)
    set(ECEWO_PLUGIN_${NAME}_DEFAULT_VERSION
      ${P_DEFAULT_VERSION}
      CACHE INTERNAL "ecewo plugin ${NAME} default version"
    )
  endif()
endfunction()

function(ecewo_add)
  foreach(SPEC IN LISTS ARGN)
    string(FIND "${SPEC}" "@" at_pos)
    if(at_pos GREATER -1)
      string(SUBSTRING "${SPEC}" 0 ${at_pos} NAME)
      math(EXPR ver_pos "${at_pos} + 1")
      string(SUBSTRING "${SPEC}" ${ver_pos} -1 VERSION)
      if(VERSION STREQUAL "")
        message(FATAL_ERROR
          "Invalid plugin spec '${SPEC}'. "
          "Expected format: name or name@version")
      endif()
    else()
      set(NAME "${SPEC}")
      set(VERSION "")
    endif()

    if(TARGET ecewo::${NAME})
      continue()
    endif()

    if(NOT DEFINED ECEWO_PLUGIN_${NAME}_REPO)
      message(FATAL_ERROR "Unknown ecewo plugin: ${NAME}")
    endif()

    if(VERSION)
      set(PLUGIN_TAG ${VERSION})
      message(STATUS "Adding ecewo plugin ${NAME} @ ${PLUGIN_TAG}")
    elseif(DEFINED ECEWO_PLUGIN_${NAME}_DEFAULT_VERSION)
      set(PLUGIN_TAG ${ECEWO_PLUGIN_${NAME}_DEFAULT_VERSION})
      message(STATUS "Adding ecewo plugin ${NAME} @ ${PLUGIN_TAG} (default)")
    else()
      set(PLUGIN_TAG main)
      message(WARNING
        "Adding ecewo plugin ${NAME} WITHOUT version pinning. "
        "Using 'main' branch (may be unstable).")
    endif()

    FetchContent_Declare(
      ecewo-${NAME}
      GIT_REPOSITORY ${ECEWO_PLUGIN_${NAME}_REPO}
      GIT_TAG ${PLUGIN_TAG}
      GIT_SHALLOW TRUE
    )

    FetchContent_MakeAvailable(ecewo-${NAME})

    if(NOT TARGET ecewo::${NAME})
      message(FATAL_ERROR
        "${NAME} plugin did not define target ecewo::${NAME}")
    endif()
  endforeach()
endfunction()

include(${CMAKE_CURRENT_LIST_DIR}/registry.cmake)
