# Contributing

Thanks for contributing to Z80 Digital Twin.

The current contributor guide lives at
[docs/developers/contributing.md](docs/developers/contributing.md).

Quick local check:

```bash
cmake -S . -B build -DZ80_BUILD_UI=OFF
cmake --build build -j
ctest --test-dir build
```
