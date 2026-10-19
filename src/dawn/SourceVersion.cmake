# Local compiler inputs are versioned before every build, while unchanged inputs retain their timestamp.
function(dawn_source_version)
    set(DAWN_SOURCE_VERSION_COMMAND "" CACHE STRING "Command that versions local compiler sources and inputs")
    if(NOT DAWN_SOURCE_VERSION_COMMAND OR NOT DAWN_VERSION_FILE)
        return()
    endif()

    if(NOT EXISTS "${DAWN_VERSION_FILE}")
        execute_process(COMMAND ${DAWN_SOURCE_VERSION_COMMAND} "${Dawn_SOURCE_DIR}" "${DAWN_VERSION_FILE}"
                        COMMAND_ERROR_IS_FATAL ANY)
    endif()

    set(toolchain CMAKE_BUILD_TYPE CMAKE_SYSTEM_NAME CMAKE_SYSTEM_PROCESSOR CMAKE_GENERATOR
        CMAKE_GENERATOR_PLATFORM CMAKE_GENERATOR_TOOLSET CMAKE_TOOLCHAIN_FILE CMAKE_SYSROOT
        CMAKE_MSVC_RUNTIME_LIBRARY CMAKE_INTERPROCEDURAL_OPTIMIZATION BUILD_SHARED_LIBS)
    get_cmake_property(variables VARIABLES)
    list(SORT variables)
    set(configuration "configuration=$<CONFIG>\n")
    foreach(name IN LISTS variables)
        if(name MATCHES "^(DAWN_|TINT_|CMAKE_(C|CXX|OBJC|OBJCXX|OSX|ANDROID)_|CMAKE_(EXE|SHARED|MODULE)_LINKER_FLAGS|CMAKE_INTERPROCEDURAL_OPTIMIZATION)"
           OR name IN_LIST toolchain)
            string(APPEND configuration "${name}=${${name}}\n")
        endif()
    endforeach()
    set(settings "${CMAKE_CURRENT_BINARY_DIR}/dawn-build-config-$<CONFIG>.txt")
    file(GENERATE OUTPUT "${settings}" CONTENT "${configuration}")
    set(inputs "${settings}")
    foreach(language C CXX OBJC OBJCXX)
        if(CMAKE_${language}_COMPILER)
            list(APPEND inputs "${CMAKE_${language}_COMPILER}")
        endif()
    endforeach()
    if(TARGET dxcompiler)
        list(APPEND inputs "$<TARGET_FILE:dxcompiler>")
    endif()

    add_custom_target(dawn_source_version
        COMMAND ${DAWN_SOURCE_VERSION_COMMAND} "${Dawn_SOURCE_DIR}" "${DAWN_VERSION_FILE}" ${inputs}
        BYPRODUCTS "${DAWN_VERSION_FILE}"
        COMMENT "Dawn: Versioning local compiler inputs"
        VERBATIM)
    if(TARGET dxcompiler)
        add_dependencies(dawn_source_version dxcompiler)
    endif()
endfunction()
