# Header dependencies with MSVC + Ninja on a machine whose compiler speaks zh-TW (see
# cmake/cl_filter.cpp): build a small launcher at configure time that rewrites cl's localized
# "/showIncludes" note to "Note: including file:", and tell Ninja to expect that prefix.
if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja")
    set(_filter_src "${CMAKE_CURRENT_LIST_DIR}/cl_filter.cpp")
    set(_filter_exe "${CMAKE_BINARY_DIR}/cl_filter/cl_filter.exe")
    if(NOT EXISTS "${_filter_exe}" OR "${_filter_src}" IS_NEWER_THAN "${_filter_exe}")
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/cl_filter")
        execute_process(
            COMMAND "${CMAKE_CXX_COMPILER}" /nologo /std:c++20 /EHsc /O2 /W4 /utf-8 /DUNICODE /D_UNICODE "${_filter_src}"
                    "/Fe${_filter_exe}" "/Fo${CMAKE_BINARY_DIR}/cl_filter/"
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/cl_filter"
            RESULT_VARIABLE _filter_rc
            OUTPUT_VARIABLE _filter_out
            ERROR_VARIABLE _filter_out)
        if(NOT _filter_rc EQUAL 0)
            message(FATAL_ERROR "cl_filter did not build:\n${_filter_out}")
        endif()
    endif()
    set(CMAKE_C_COMPILER_LAUNCHER "${_filter_exe}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${_filter_exe}")
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "Note: including file:")
endif()
