@echo off
g++ -D_WIN32_WINNT=0x0A00 ^
-DCPPHTTPLIB_OPENSSL_SUPPORT ^
src/main.cpp ^
-o proxy.exe ^
-std=c++11 ^
-Iinclude ^
-ID:\Project\vcpkg-master\installed\x64-mingw-static\include ^
-LD:\Project\vcpkg-master\installed\x64-mingw-static\lib ^
-lssl -lcrypto ^
-lpthread -lws2_32 -lcrypt32 -lgdi32 -ladvapi32 -luser32
pause
