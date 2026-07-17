cmake_minimum_required(VERSION 3.20)

set(WEBAPP_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/app.json" CACHE FILEPATH "Website desktop application JSON configuration")
option(WEBAPP_USE_GENERATED "Use the already-generated application configuration and metadata" OFF)
option(WEBAPP_ENABLE_DEBUG_TOOLS "Enable WebView developer tools by default" OFF)
set(CHOC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/subprojects/choc/choc" CACHE PATH "Path to the CHOC headers")

if(NOT WEBAPP_USE_GENERATED)
    find_program(
        WEBAPP_PYTHON_EXECUTABLE
        NAMES python3 python
        HINTS
            "${CMAKE_CURRENT_SOURCE_DIR}/.venv/bin"
            "${CMAKE_CURRENT_SOURCE_DIR}/.venv/Scripts"
        REQUIRED
    )

    execute_process(
        COMMAND
            "${WEBAPP_PYTHON_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/configure_app.py"
            --root "${CMAKE_CURRENT_SOURCE_DIR}"
            --config "${WEBAPP_CONFIG_FILE}"
            --output "${CMAKE_CURRENT_SOURCE_DIR}/generated"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE WEBAPP_GENERATOR_RESULT
        OUTPUT_VARIABLE WEBAPP_GENERATOR_OUTPUT
        ERROR_VARIABLE WEBAPP_GENERATOR_ERROR
    )

    if(NOT WEBAPP_GENERATOR_RESULT EQUAL 0)
        message(FATAL_ERROR
            "Application configuration generation failed.\n"
            "${WEBAPP_GENERATOR_OUTPUT}${WEBAPP_GENERATOR_ERROR}"
        )
    endif()
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/app_config.cmake")
    message(FATAL_ERROR
        "generated/app_config.cmake is missing. Run ./scripts/configure-app.sh first, "
        "or configure CMake without -DWEBAPP_USE_GENERATED=ON."
    )
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/generated/app_config.cmake")

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "${WEBAPP_MACOS_MINIMUM_SYSTEM_VERSION}" CACHE STRING "Minimum macOS version" FORCE)
endif()

project(
    website_desktop_app
    VERSION "${WEBAPP_VERSION}"
    DESCRIPTION "${WEBAPP_DESCRIPTION}"
    HOMEPAGE_URL "${WEBAPP_HOMEPAGE_URL}"
    LANGUAGES CXX
)

include(GNUInstallDirs)

if(NOT EXISTS "${CHOC_ROOT}/gui/choc_WebView.h")
    message(FATAL_ERROR
        "The CHOC dependency is missing. Run ./scripts/bootstrap.sh, or clone the repository with --recurse-submodules."
    )
endif()

set(WEBAPP_SOURCES src/main.cpp)
set(WEBAPP_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated")

if(WIN32)
    file(TO_CMAKE_PATH "${WEBAPP_GENERATED_DIR}/assets/icon.ico" WEBAPP_WINDOWS_ICON)
    configure_file(
        "${WEBAPP_GENERATED_DIR}/app.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/app.rc"
        @ONLY
    )
    list(APPEND WEBAPP_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
    add_executable("${WEBAPP_EXECUTABLE}" WIN32 ${WEBAPP_SOURCES})
elseif(APPLE)
    set_source_files_properties(
        "${WEBAPP_GENERATED_DIR}/assets/icon.icns"
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources
    )
    list(APPEND WEBAPP_SOURCES "${WEBAPP_GENERATED_DIR}/assets/icon.icns")
    add_executable("${WEBAPP_EXECUTABLE}" MACOSX_BUNDLE ${WEBAPP_SOURCES})
else()
    add_executable("${WEBAPP_EXECUTABLE}" ${WEBAPP_SOURCES})
endif()

set(WEBAPP_TARGET "${WEBAPP_EXECUTABLE}")
target_compile_features("${WEBAPP_TARGET}" PRIVATE cxx_std_17)
target_include_directories(
    "${WEBAPP_TARGET}"
    SYSTEM PRIVATE "${CHOC_ROOT}"
)
target_include_directories(
    "${WEBAPP_TARGET}"
    PRIVATE "${WEBAPP_GENERATED_DIR}"
)

if(WEBAPP_ENABLE_DEBUG_TOOLS)
    target_compile_definitions("${WEBAPP_TARGET}" PRIVATE WEBAPP_DEBUG_DEFAULT=1)
endif()

if(MSVC)
    target_compile_options("${WEBAPP_TARGET}" PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(
        "${WEBAPP_TARGET}"
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wconversion
    )
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    find_package(Threads REQUIRED)
    pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
    pkg_check_modules(WEBKIT2GTK REQUIRED IMPORTED_TARGET webkit2gtk-4.1)

    target_link_libraries(
        "${WEBAPP_TARGET}"
        PRIVATE
            Threads::Threads
            PkgConfig::GTK3
            PkgConfig::WEBKIT2GTK
    )
elseif(APPLE)
    target_compile_options("${WEBAPP_TARGET}" PRIVATE -fblocks)
    target_link_libraries(
        "${WEBAPP_TARGET}"
        PRIVATE
            "-framework Cocoa"
            "-framework Foundation"
            "-framework WebKit"
            "-framework CoreServices"
    )

    set_target_properties(
        "${WEBAPP_TARGET}"
        PROPERTIES
            OUTPUT_NAME "${WEBAPP_SLUG}"
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_BUNDLE_NAME "${WEBAPP_NAME}"
            MACOSX_BUNDLE_GUI_IDENTIFIER "${WEBAPP_ID}"
            MACOSX_BUNDLE_INFO_PLIST "${WEBAPP_GENERATED_DIR}/Info.plist"
            MACOSX_BUNDLE_ICON_FILE "icon.icns"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${WEBAPP_VERSION}"
            MACOSX_BUNDLE_BUNDLE_VERSION "${WEBAPP_VERSION}"
    )
endif()

install(
    TARGETS "${WEBAPP_TARGET}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    BUNDLE DESTINATION "."
)

if(UNIX AND NOT APPLE)
    install(
        FILES "${WEBAPP_GENERATED_DIR}/packaging/linux/${WEBAPP_ID}.desktop"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
    )
    install(
        FILES "${WEBAPP_GENERATED_DIR}/packaging/linux/${WEBAPP_ID}.metainfo.xml"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/metainfo"
    )

    foreach(ICON_SIZE IN ITEMS 16 32 48 64 128 256 512)
        install(
            FILES "${WEBAPP_GENERATED_DIR}/assets/icon-${ICON_SIZE}.png"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${ICON_SIZE}x${ICON_SIZE}/apps"
            RENAME "${WEBAPP_ID}.png"
        )
    endforeach()
endif()
