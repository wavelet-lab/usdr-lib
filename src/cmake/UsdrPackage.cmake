# Usdr CMake package helper
# Provides a function `usdr_install_package(NAME <name> VERSION <ver> TARGETS <exported_targets> PKGCONFIG <ON|OFF>)`

include(CMakePackageConfigHelpers)
get_filename_component(USDR_PACKAGE_HELPER_DIR "${CMAKE_CURRENT_LIST_FILE}" PATH)

function(usdr_install_package)
    cmake_parse_arguments(_u "" "NAME;VERSION;PKGCONFIG" "TARGETS" ${ARGN})

    if(NOT _u_NAME)
        message(FATAL_ERROR "usdr_install_package: NAME is required")
    endif()
    if(NOT _u_VERSION)
        message(FATAL_ERROR "usdr_install_package: VERSION is required")
    endif()

    set(_install_dir "${CMAKE_INSTALL_LIBDIR}/cmake/${_u_NAME}")

    # Helper dir (set at include-time) points to this file's directory
    set(_usdr_helper_dir "${USDR_PACKAGE_HELPER_DIR}")

    # Configure and write CMake package config files
    set(_config_in "${_usdr_helper_dir}/${_u_NAME}Config.cmake.in")
    if(NOT IS_ABSOLUTE "${_config_in}")
        get_filename_component(_config_in "${_config_in}" REALPATH BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()

    configure_package_config_file(
        "${_config_in}"
        "${CMAKE_CURRENT_BINARY_DIR}/${_u_NAME}Config.cmake"
        INSTALL_DESTINATION "${_install_dir}"
    )

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/${_u_NAME}ConfigVersion.cmake"
        VERSION ${_u_VERSION}
        COMPATIBILITY AnyNewerVersion
    )

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/${_u_NAME}Config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/${_u_NAME}ConfigVersion.cmake"
        DESTINATION "${_install_dir}"
    )

    # Install exported CMake targets
    if(_u_TARGETS)
        install(EXPORT ${_u_TARGETS}
            FILE ${_u_NAME}Targets.cmake
            NAMESPACE ${_u_NAME}::
            DESTINATION "${_install_dir}"
        )
    endif()

    # Optionally generate and install a pkg-config file
    if(_u_PKGCONFIG STREQUAL "ON")
        set(_pc_in "${_usdr_helper_dir}/${_u_NAME}.pc.in")
        if(NOT IS_ABSOLUTE "${_pc_in}")
            get_filename_component(_pc_in "${_pc_in}" REALPATH BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        endif()

        # Provide substitutions for the pkg-config template
        set(PACKAGE_NAME "${_u_NAME}")
        set(PACKAGE_VERSION "${_u_VERSION}")

        configure_file("${_pc_in}"
                       "${CMAKE_CURRENT_BINARY_DIR}/${_u_NAME}.pc" @ONLY)
        unset(PACKAGE_NAME CACHE)
        unset(PACKAGE_VERSION CACHE)
        install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${_u_NAME}.pc"
                DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
    endif()
endfunction()
