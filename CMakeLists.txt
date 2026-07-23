cmake_minimum_required(VERSION 3.16)
project(llm-proxy VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# httplib 需要此宏才启用 SSL 支持
add_definitions(-DCPPHTTPLIB_OPENSSL_SUPPORT)

# Windows 10 SDK 版本 (仅 MinGW / MSVC 使用)
if(WIN32)
    add_definitions(-D_WIN32_WINNT=0x0A00)
endif()

# MinGW 静态链接 libgcc / libstdc++（减小分发依赖）
if(MINGW)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libgcc -static-libstdc++")
endif()

# include 目录（header-only 库）
include_directories(${CMAKE_SOURCE_DIR}/include)

# 依赖：OpenSSL + 线程
# 若 OpenSSL 未自动发现，手动指定：
#   MSYS2 pacman 安装后：cmake -B build -G "MSYS Makefiles" ..
#   vcpkg：cmake -B build -G "MinGW Makefiles" -DOPENSSL_ROOT_DIR=<vcpkg-path>
find_package(OpenSSL REQUIRED
    HINTS ${OPENSSL_ROOT_DIR}
)
find_package(Threads REQUIRED)

# 源文件
add_executable(proxy src/main.cpp)

# 通用链接
target_link_libraries(proxy
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)

# Windows 专属系统库
if(WIN32)
    target_link_libraries(proxy
        ws2_32
        crypt32
        gdi32
        advapi32
        user32
    )
endif()
