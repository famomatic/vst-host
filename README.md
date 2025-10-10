git submodule update --init --recursive

cmake -B build -G "Visual Studio 17 2022" -A x64

cmake --build build --config Release

버그 고치는데 오래 걸릴 것 같네요
