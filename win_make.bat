@echo off

del /q .\build\client.exe 2>nul
gcc --static -std=c99 -g -o build/client src/client.c -lpthread -lws2_32 -lwinmm -liphlpapi
