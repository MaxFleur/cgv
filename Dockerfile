FROM henne90gen/opengl:3.1

RUN echo "#!/usr/bin/env bash \n\
mkdir -p build \n\
cd build || exit 0 \n\
cmake .. -G Ninja -D CMAKE_BUILD_TYPE=Release -D CMAKE_INSTALL_PREFIX=../install -D CMAKE_C_COMPILER=/usr/bin/clang-13 -D CMAKE_CXX_COMPILER=/usr/bin/clang++-13 -D SHADER_DEVELOPER=FALSE \n\
cmake --build . --target install\n" > /build.sh

RUN chmod +x /build.sh
CMD ["/build.sh"]
