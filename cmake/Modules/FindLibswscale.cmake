# Once done these will be defined:
#
#  Libswscale_FOUND
#  Libswscale_INCLUDE_DIR
#  Libswscale_LIBRARIES
#

if(Libswscale_INCLUDE_DIR AND Libswscale_LIBRARIES)
	set(Libswscale_FOUND TRUE)
else()
	find_package(PkgConfig QUIET)
	if (PKG_CONFIG_FOUND)
		pkg_check_modules(_SWSCALE QUIET libswscale)
	endif()

	if(CMAKE_SIZEOF_VOID_P EQUAL 8)
		set(_lib_suffix 64)
	else()
		set(_lib_suffix 32)
	endif()
	
	find_path(FFMPEG_INCLUDE_DIR
		NAMES libswscale/swscale.h
		HINTS
			ENV FFmpegPath
			${_SWSCALE_INCLUDE_DIRS}
			/usr/include /usr/local/include /opt/local/include /sw/include
		PATH_SUFFIXES ffmpeg libav)

	find_library(SWSCALE_LIB
		NAMES swscale
		HINTS ${FFMPEG_INCLUDE_DIR}/../lib ${FFMPEG_INCLUDE_DIR}/lib${_lib_suffix} ${_SWSCALE_LIBRARY_DIRS} /usr/lib /usr/local/lib /opt/local/lib /sw/lib)

	set(Libswscale_INCLUDE_DIR ${FFMPEG_INCLUDE_DIR} CACHE PATH "Libswscale include dir")
	set(Libswscale_LIBRARIES ${SWSCALE_LIB} CACHE STRING "Libswscale libraries")

	find_package_handle_standard_args(Libswscale DEFAULT_MSG SWSCALE_LIB FFMPEG_INCLUDE_DIR)
	mark_as_advanced(FFMPEG_INCLUDE_DIR SWSCALE_LIB)
endif()

