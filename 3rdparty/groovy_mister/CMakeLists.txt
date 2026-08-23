add_library(3rdparty_groovy_mister STATIC EXCLUDE_FROM_ALL
	groovymister.cpp
	groovymister_wrapper.cpp
	nlc_codec.cpp
	lz4/lz4.c
	lz4/lz4hc.c
)

# Vendored from https://github.com/psakhis/Groovy_MiSTer (api/)
# Build statically into rpcs3 so there is no runtime dlopen surface.
target_compile_definitions(3rdparty_groovy_mister PUBLIC
	GROOVYMISTER_WIN32_STATIC
)

target_include_directories(3rdparty_groovy_mister SYSTEM PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

if(WIN32)
	# RIO (registered I/O) for the low-latency UDP sender, plus standard winsock.
	target_link_libraries(3rdparty_groovy_mister PUBLIC ws2_32 mswsock)
endif()

if(NOT MSVC)
	# 3rdparty code is built warning-free.
	target_compile_options(3rdparty_groovy_mister PRIVATE -w)
endif()

set_property(TARGET 3rdparty_groovy_mister PROPERTY FOLDER "3rdparty/")
