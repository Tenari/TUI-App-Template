#!/bin/sh

if [ "$1" = "server" ]; then
  echo "building server"
  rm ./build/server
  rm -rf ./build/server.dSYM
  if [[ $(uname) == "Darwin" ]]; then
    echo " for macOS"
    gcc -std=c99 -g -o build/server src/server.c -lpthread
  else
    echo " for Linux"
    gcc -std=c99 -D_POSIX_C_SOURCE=200809L -g -o build/server src/server.c -lpthread
  fi
  if [ "$2" = "run" ]; then
    ./build/server
  elif [ "$2" = "debug-run" ]; then
    ../lldbg/bin/lldbgui ./build/server
  fi
elif [ "$1" = "editor" ]; then
  echo "building editor"
  rm ./build/editor
  rm -rf ./build/editor.dSYM
  gcc -std=c99 -g -o build/editor src/room_editor.c
  if [ "$2" = "run" ]; then
    ./build/editor $3
  fi
else
  #echo "building assets"
  #for item in "asset1" "asset2" "asset3"; do
  #  rm "src/assets/$item.h"
  #  xxd -i "assets/$item.txt" > "src/assets/$item.h"
  #done

  echo "building client"
  rm ./build/client
  rm -rf ./build/client.dSYM
  gcc -std=c99 -g -o build/client src/client.c -lpthread
  if [ "$2" = "run" ]; then
    if [ "$3" ]; then
      ./build/client $3
    else
      ./build/client
    fi
  elif [ "$2" = "debug-run" ]; then
    ../lldbg/bin/lldbgui ./build/client
  fi
fi
