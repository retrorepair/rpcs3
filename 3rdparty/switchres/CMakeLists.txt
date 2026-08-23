add_library(3rdparty_switchres STATIC EXCLUDE_FROM_ALL
	switchres.cpp
	monitor.cpp
	modeline.cpp
	display.cpp
	custom_video.cpp
	log.cpp
	edid.cpp
	switchres_wrapper.cpp
)

# Vendored from https://github.com/antonioginer/switchres (deps/switchres/).
# Calculate-only build: SR_CALC_ONLY drops every real host-display backend
# (display_linux/windows/sdl2 + their X11/DRM/ADL/ATI custom_video deps) and
# keeps only the "dummy" backend. We use switchres purely as a modeline
# calculator and send the result to the MiSTer over UDP; we never switch a
# host display. SR_WIN32_STATIC makes the wrapper symbols non-dllexport for
# static linking (same trick as 3rdparty_groovy_mister).
target_compile_definitions(3rdparty_switchres PUBLIC
	SR_CALC_ONLY
	SR_WIN32_STATIC
)

target_include_directories(3rdparty_switchres SYSTEM PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

if(NOT MSVC)
	# 3rdparty code is built warning-free.
	target_compile_options(3rdparty_switchres PRIVATE -w)
endif()

set_property(TARGET 3rdparty_switchres PROPERTY FOLDER "3rdparty/")
