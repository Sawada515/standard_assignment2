#!/bin/bash

# cmakeを実行する際のオプションが増えたため

if [[ $1 == "debug" ]]; then
    cd ./build/
    
    cmake -DCMAKE_BUILD_TYPE=Debug \
            -D WITH_JPEG=ON \
            -D BUILD_JPEG=OFF \
            -D JPEG_INCLUDE_DIR=/usr/include/ \
            -D JPEG_LIBRARY=/usr/lib/x86_64-linux-gnu/libjpeg.so \
            ..
else
    cd ./build/
    cmake -DCMAKE_BUILD_TYPE=Release \
            -D WITH_JPEG=ON \
            -D BUILD_JPEG=OFF \
            -D JPEG_INCLUDE_DIR=/usr/include/ \
            -D JPEG_LIBRARY=/usr/lib/x86_64-linux-gnu/libjpeg.so \
            ..
fi
