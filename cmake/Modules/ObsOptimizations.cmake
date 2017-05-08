# central place to define compiler optimizations for architectures
# we enable some optimizations by default:
# mmx, sse, sse2

macro(check_def default name)
	if((${default} AND NOT DISABLE_C_FLAGS) OR ENABLE_${name})
		set(HAVE_${name} 1)
	endif()
endmacro()

macro(add_flag test flag)
	if(${test})
		set(OBS_C_FLAGS "${OBS_C_FLAGS}${flag} ")
		add_definitions("-D${test}=1")
	endif()
endmacro()

# check for configure options
check_def(1 "MMX")
check_def(1 "SSE")
check_def(1 "SSE2")
check_def(0 "AVX")
check_def(0 "AVX2")

# add compiler flags
set(OBS_C_FLAGS "")
if(MSVC)
	# msvc is kind of special
	add_flag(HAVE_SSE       "/arch:SSE")
	add_flag(HAVE_SSE2      "/arch:SSE2")
	add_flag(HAVE_AVX       "/arch:AVX")
	add_flag(HAVE_AVX2      "/arch:AVX2")
else()
	# clang and gcc share the same flags
	add_flag(HAVE_MMX       "-mmmx")
	add_flag(HAVE_SSE       "-msse")
	add_flag(HAVE_SSE2      "-msse2")
	add_flag(HAVE_AVX       "-mavx")
	add_flag(HAVE_AVX2      "-mavx2")
endif()

# add custom flags supplied by the user
set(OBS_C_FLAGS "${OBS_C_FLAGS}${CUSTOM_C_FLAGS}")

# print message and pass flags to cmake
if (OBS_C_FLAGS)
	MESSAGE(STATUS "Building with the optimizations: ${OBS_C_FLAGS}")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OBS_C_FLAGS}")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OBS_C_FLAGS}")
else()
	MESSAGE(STATUS "Building without optimizations")
endif()
