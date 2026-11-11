if (NOT NO_COTIRE)
	include(cotire)
endif()

# wxWidgets version minimum 3.0
if(NOT WX_VERSION)
	SET(WX_VERSION 3.0)
endif(NOT WX_VERSION)

# wxWidgets libs
if (WITH_WXPATH)
    set(ENV{PATH} ${WITH_WXPATH}:$ENV{PATH})
endif()
unset(WITH_WXPATH CACHE)

set(CL_WX_CONFIG wx-config CACHE STRING "")

if (UNIX OR MINGW)
    if(NOT wxWidgets_CONFIG_EXECUTABLE)
        execute_process(COMMAND which ${CL_WX_CONFIG} OUTPUT_VARIABLE WX_TOOL OUTPUT_STRIP_TRAILING_WHITESPACE)
    else()
        set(WX_TOOL ${wxWidgets_CONFIG_EXECUTABLE})
    endif()
    if (NOT WX_TOOL)
        message(FATAL_ERROR
"\nNo functional wx_config script was found in your PATH.\nIs the wxWidgets development package installed?\nIf you built wxWidgets yourself, you can specify the path to your built wx-config executable via WITH_WXPATH\neg. -DWITH_WXPATH=\"/path/to/wx-config/\""
             )
    else()
        execute_process(COMMAND sh ${WX_TOOL} --version OUTPUT_VARIABLE WX_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
        string(SUBSTRING "${WX_VERSION}" "0" "1" wxMAJOR_VERSION)
        string(SUBSTRING "${WX_VERSION}" "2" "1" wxMINOR_VERSION)
        string(SUBSTRING "${WX_VERSION}" "4" "1" wxRELEASE_NUMBER)
        if ( wxMAJOR_VERSION LESS 3 )
        message(FATAL_ERROR
"\nBuilding SLADE requires at least wxWidgets-3.0.0"
             )
        endif()
        if (MINGW)
          execute_process(COMMAND sh ${WX_TOOL} --debug=no --rescomp OUTPUT_VARIABLE WX_RC_FLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
          string(REGEX REPLACE "windres" "" WX_RC_FLAGS ${WX_RC_FLAGS})
          set (CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} ${WX_RC_FLAGS}")
          add_definitions(-D__WXMSW__)
        endif (MINGW)
    endif()
    message("-- wx-config used is: ${WX_TOOL}")
    message("-- wxWidgets version is: ${WX_VERSION}")
    if (NOT APPLE AND NOT MINGW)
        # Is the wx we are using built on gtk2 or 3?
        execute_process(COMMAND ${WX_TOOL} --selected_config OUTPUT_VARIABLE WX_GTK_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
        string(SUBSTRING "${WX_GTK_VERSION}" "3" "1" GTK_VERSION)
        message("-- gtk version is: ${GTK_VERSION}")
    endif()
	set(wxWidgets_CONFIG_EXECUTABLE "${WX_TOOL}")
endif (UNIX OR MINGW)

if (WX_GTK3)
	set(wxWidgets_CONFIG_OPTIONS --toolkit=gtk3)
endif (WX_GTK3)

SET(WX_LIBS std aui gl stc richtext propgrid)
find_package(wxWidgets ${WX_VERSION} COMPONENTS ${WX_LIBS} REQUIRED)
include(${wxWidgets_USE_FILE})

# SFML
if (USE_SFML_RENDERWINDOW)
set(SFML_FIND_COMPONENTS system audio window graphics network)
ADD_DEFINITIONS(-DUSE_SFML_RENDERWINDOW)
else (USE_SFML_RENDERWINDOW)
set(SFML_FIND_COMPONENTS system audio window network)
find_package(Freetype REQUIRED)
find_package(FTGL REQUIRED)
endif(USE_SFML_RENDERWINDOW)

# Fluidsynth
if (NO_FLUIDSYNTH)
ADD_DEFINITIONS(-DNO_FLUIDSYNTH)
endif(NO_FLUIDSYNTH)

if (CMAKE_INSTALL_PREFIX)
ADD_DEFINITIONS(-DINSTALL_PREFIX="${CMAKE_INSTALL_PREFIX}")
endif(CMAKE_INSTALL_PREFIX)

if (NOT APPLE)
	find_package (PkgConfig REQUIRED)
if (WX_GTK3)
	pkg_check_modules (GTK3 REQUIRED gtk+-3.0)
else (WX_GTK3)
	pkg_check_modules (GTK2 REQUIRED gtk+-2.0)
endif (WX_GTK3)
endif (NOT APPLE)

if(NOT NO_FLUIDSYNTH)
	find_package(FluidSynth REQUIRED)
else(NO_FLUIDSYNTH)
	message(STATUS "Fluidsynth support is disabled.")
endif()

find_package(FreeImage REQUIRED)
find_package(SFML COMPONENTS ${SFML_FIND_COMPONENTS} REQUIRED)
find_package(OpenGL REQUIRED)
if (NOT NO_LUA)
	find_package(Lua REQUIRED)
endif()
include_directories(
	${FREEIMAGE_INCLUDE_DIR}
	${SFML_INCLUDE_DIR}
	${FREETYPE_INCLUDE_DIRS}
	${FTGL_INCLUDE_DIR}
	${LUA_INCLUDE_DIR}
	.
	..
	../thirdparty/glad/include
	./Application
	)

if (WX_GTK3)
	include_directories(${GTK3_INCLUDE_DIRS})
else (WX_GTK3)
	include_directories(${GTK2_INCLUDE_DIRS})
endif (WX_GTK3)

if (NOT NO_FLUIDSYNTH)
	include_directories(${FLUIDSYNTH_INCLUDE_DIR})
endif()

set(SLADE64_SOURCES
)
set(SLADE64_SCRIPTING_SOURCES)
# Don't include external libraries here as they should be compiled separately
file(GLOB_RECURSE SLADE64_SOURCES
	Application/*.cpp
	Archive/*.cpp
	Audio/*.cpp
	Dialogs/*.cpp
	Game/*.cpp
	General/*.cpp
	Graphics/*.cpp
	MainEditor/*.cpp
	MapEditor/*.cpp
	OpenGL/*.cpp
	SLADE64Map/*.cpp
	TextEditor/*.cpp
	UI/*.cpp
	Utility/*.cpp
	)
if (NOT NO_LUA)
	file(GLOB_RECURSE SLADE64_SCRIPTING_SOURCES Scripting/*.cpp)
	set(SLADE64_SOURCES ${SLADE64_SOURCES} ${SLADE64_SCRIPTING_SOURCES})
else ()
	ADD_DEFINITIONS(-DNO_LUA)
endif ()
set(SLADE64_HEADERS
)
file(GLOB_RECURSE SLADE64_HEADERS *.h *.hpp)

if(APPLE)
	set(OSX_ICON "${CMAKE_SOURCE_DIR}/SLADE-osx.icns")
	set(OSX_PLIST "${CMAKE_SOURCE_DIR}/Info.plist")

	set(SLADE64_SOURCES ${SLADE64_SOURCES} ${OSX_ICON} ${OSX_PLIST})

	set_source_files_properties(${OSX_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif(APPLE)

# Enable SSE instructions for dumb library
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_USE_SSE -msse")

# Enable debug symbols for glib (so gdb debugging works properly with strings etc.)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -D_GLIBCXX_DEBUG")

if(USE_SANITIZER)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
endif(USE_SANITIZER)

# External libraries are compiled separately to enable unity builds
add_subdirectory(../thirdparty external)

add_executable(slade64 WIN32 MACOSX_BUNDLE
	${SLADE64_SOURCES}
	${SLADE64_HEADERS}
)

target_link_libraries(slade64
	${ZLIB_LIBRARY}
	${BZIP2_LIBRARIES}
	${EXTERNAL_LIBRARIES}
	${wxWidgets_LIBRARIES}
	${FREEIMAGE_LIBRARIES}
	${SFML_LIBRARY}
	${FREETYPE_LIBRARIES}
	${FTGL_LIBRARIES}
	${OPENGL_LIBRARIES}
	${LUA_LIBRARIES}
)

if(LINUX)
	target_link_libraries(slade64 -lstdc++fs)
endif()

if (WX_GTK3)
	target_link_libraries(slade64 ${GTK3_LIBRARIES})
else(WX_GTK3)
	target_link_libraries(slade64 ${GTK2_LIBRARIES})
endif(WX_GTK3)

if (NOT NO_FLUIDSYNTH)
	target_link_libraries(slade64 ${FLUIDSYNTH_LIBRARIES})
endif()

set_target_properties(slade64 PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${SLADE64_OUTPUT_DIR})

# TODO: Installation targets for APPLE
if(APPLE)
	set_target_properties(slade64 PROPERTIES MACOSX_BUNDLE_INFO_PLIST ${OSX_PLIST})

	add_custom_command(TARGET slade64 POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SLADE64_OUTPUT_DIR}/slade64.pk3" "$<TARGET_FILE_DIR:slade64>/slade64.pk3"
	)
else(APPLE)
	if(UNIX)
		install(TARGETS slade64
			RUNTIME DESTINATION bin
			)

		install(FILES "${SLADE64_OUTPUT_DIR}/slade64.pk3"
			DESTINATION share/slade64
			)

		install(FILES "${PROJECT_SOURCE_DIR}/dist/res/logo_icon.png"
			DESTINATION share/icons/
			RENAME net.mancubus.SLADE64.png
			)

		install(FILES "${PROJECT_SOURCE_DIR}/net.mancubus.SLADE64.desktop"
			DESTINATION share/applications/
			)

		install(FILES "${PROJECT_SOURCE_DIR}/net.mancubus.SLADE64.metainfo.xml"
			DESTINATION share/metainfo/
			)
	endif(UNIX)
endif(APPLE)

# uninstall target
if(NOT TARGET uninstall)
    configure_file(
        "${PROJECT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
        IMMEDIATE @ONLY)

    add_custom_target(uninstall
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
endif()

if (NOT NO_COTIRE)
	set_target_properties(slade64 PROPERTIES
		COTIRE_CXX_PREFIX_HEADER_INIT "common.h"
		# Enable multithreaded unity builds by default
		# because otherwise probably no one would realize how
		COTIRE_UNITY_SOURCE_MAXIMUM_NUMBER_OF_INCLUDES -j
		# Fixes macro definition bleedout
		COTIRE_UNITY_SOURCE_PRE_UNDEFS "Bool"
		)
	cotire(slade64)
endif()
