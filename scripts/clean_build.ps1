$ErrorActionPreference = 'Stop'

Remove-Item build -Recurse -Force -ErrorAction Ignore
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
