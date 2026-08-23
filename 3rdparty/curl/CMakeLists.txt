# CURL

if(USE_SYSTEM_CURL)
	message(STATUS "RPCS3: using shared libcurl")
	find_package(CURL REQUIRED)
	add_library(3rdparty_libcurl INTERFACE)
	target_link_libraries(3rdparty_libcurl INTERFACE CURL::libcurl)
else()
	message(STATUS "RPCS3: building libcurl + wolfssl submodules")
	set(BUILD_CURL_EXE OFF CACHE INTERNAL "")
	set(BUILD_STATIC_CURL OFF CACHE INTERNAL "")
	set(BUILD_STATIC_LIBS ON CACHE INTERNAL "")
	set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "")
	find_package(WolfSSL REQUIRED)
	set(CURL_USE_WOLFSSL ON CACHE INTERNAL "")
	set(CURL_USE_OPENSSL OFF CACHE INTERNAL "")
	set(HTTP_ONLY ON CACHE INTERNAL "")
	set(USE_LIBIDN2 OFF CACHE INTERNAL "") # Disabled because MacOS CI doesn't work otherwise
	set(CURL_CA_PATH "none" CACHE INTERNAL "")
	set(CURL_DISABLE_INSTALL ON CACHE INTERNAL "")
	set(CURL_ENABLE_EXPORT_TARGET OFF CACHE INTERNAL "")
	if(WIN32)
		set(ENABLE_UNICODE ON CACHE INTERNAL "")
	endif()
	set(CURL_USE_LIBSSH2 OFF CACHE INTERNAL "")
	set(CURL_USE_LIBPSL OFF CACHE INTERNAL "")
	set(BUILD_TESTING OFF CACHE INTERNAL "")
	set(BUILD_EXAMPLES OFF CACHE INTERNAL "")

	# RPCS3 LOCAL PATCH (build-infra, not feature code): RPCS3 builds zlib
	# static, so the bundled zlib exposes only `zlibstatic` (it creates
	# ZLIB::ZLIB only for the shared lib). curl 8.20's
	# curl_openssl_check_exists() unconditionally appends ZLIB::ZLIB to
	# CMAKE_REQUIRED_LIBRARIES then runs check_function_exists() ->
	# try_compile(); the scratch project cannot see that (missing) target
	# so configure hard-fails. Fix:
	#  1. Pre-seed the wolfSSL feature-check result cache vars so
	#     check_function_exists() short-circuits and never runs the broken
	#     try_compile. Values mirror RPCS3's actual wolfSSL build options
	#     (3rdparty/wolfssl/CMakeLists.txt): OPENSSLEXTRA+DES3 ON (=1),
	#     ALPN/OPENSSLALL/QUIC NOT enabled (=0). Setting the BIO/ALPN ones
	#     to 1 would make curl compile code for absent wolfSSL APIs and
	#     fail at link (LNK2019 wolfSSL_UseALPN / wolfSSL_BIO_set_shutdown).
	#  2. Alias ZLIB::ZLIB -> zlibstatic so curl's real (non-try_compile)
	#     link still resolves at main-project scope.
	set(HAVE_WOLFSSL_GET_PEER_CERTIFICATE 1 CACHE INTERNAL "RPCS3 LOCAL PATCH: present (WOLFSSL_OPENSSLEXTRA=ON); skip curl try_compile (missing ZLIB::ZLIB)" FORCE)
	set(HAVE_WC_DES_ECBENCRYPT 1 CACHE INTERNAL "RPCS3 LOCAL PATCH: present (WOLFSSL_DES3=ON); skip curl try_compile" FORCE)
	set(HAVE_WOLFSSL_USEALPN 0 CACHE INTERNAL "RPCS3 LOCAL PATCH: absent (WOLFSSL_ALPN not enabled); skip curl try_compile" FORCE)
	set(HAVE_WOLFSSL_BIO_NEW 0 CACHE INTERNAL "RPCS3 LOCAL PATCH: wolfSSL BIO compat needs OPENSSL_ALL (not enabled); keep curl off its wolfSSL-BIO path" FORCE)
	set(HAVE_WOLFSSL_BIO_SET_SHUTDOWN 0 CACHE INTERNAL "RPCS3 LOCAL PATCH: absent without OPENSSL_ALL" FORCE)
	set(HAVE_WOLFSSL_SET_QUIC_USE_LEGACY_CODEPOINT 0 CACHE INTERNAL "RPCS3 LOCAL PATCH: WOLFSSL_QUIC off; curl_openssl_check_quic() unreached under HTTP_ONLY" FORCE)
	if(NOT TARGET ZLIB::ZLIB AND TARGET zlibstatic)
		add_library(ZLIB::ZLIB ALIAS zlibstatic)
	endif()

	add_subdirectory(curl EXCLUDE_FROM_ALL)

	add_library(3rdparty_libcurl INTERFACE)
	target_link_libraries(3rdparty_libcurl INTERFACE libcurl_static)

endif()
