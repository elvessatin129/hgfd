cmake_minimum_required(VERSION 3.16)
project(teamviewer_login_cpp VERSION 0.1.0 LANGUAGES CXX)

# Force static runtime only if desired. Default OFF for portability.
option(STATIC_RUNTIME "Link static runtime (/MT)" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  if(STATIC_RUNTIME)
    foreach(flag_var CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
                    CMAKE_C_FLAGS_MINSIZEREL CMAKE_C_FLAGS_RELWITHDEBINFO
                    CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
                    CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
      if(${flag_var} MATCHES "/MD")
        string(REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
      endif()
    endforeach()
  endif()
endif()

add_executable(teamviewer_login
  src/main.cpp
)

if(WIN32)
  target_compile_definitions(teamviewer_login PRIVATE UNICODE _UNICODE)
  target_link_libraries(teamviewer_login PRIVATE winhttp)
endif()

# Helpful warnings
if(MSVC)
  target_compile_options(teamviewer_login PRIVATE /permissive- /W4 /WX-)
else()
  target_compile_options(teamviewer_login PRIVATE -Wall -Wextra -Wpedantic)
endif()
