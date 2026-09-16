# Contributing

Issues and pull requests are welcome.

- Each case under `ut/` is an end-to-end scenario; treat the existing tests as the primary usage examples.
- Changes should include or update unit tests.
- CI (`UT + valgrind`) must pass: GoogleTest under Valgrind, plus the gcovr line/branch floors in `ut/gcovr/gcovr.cfg`.
- The currently verified CI environment is Ubuntu + GCC. Local builds need CMake 3.16+ and a C++17 compiler.

Contact: fchn289@gmail.com
