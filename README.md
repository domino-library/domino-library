# Domino Library

A small C++17 library.

Talk is cheap. Let $\color{red}{\textsf{AI}}$ show you the library details:
- What it does
- How it works
- Quality/proficiency, e.g.:
  - **AI benchmark:** Give Claude Opus 4.5 only [SafePtr's UTs](ut/safe_mem) and ask it to produce its best implementation that passes every test. Then compare the generated implementation with the [existing code](src/safe_mem) under the same rubric. **Result in my run: existing SafePtr 92/100; Opus baseline 68/100.**

[![Build Status](https://github.com/domino-library/domino-library/actions/workflows/ci.yml/badge.svg)](https://github.com/domino-library/domino-library/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/domino-library/domino-library/branch/main/graph/badge.svg?token=intbnIJQBV)](https://codecov.io/gh/domino-library/domino-library)
[![License](https://img.shields.io/badge/license-BSD--3--Clause-blue.svg)](LICENSE)

![Domino tiles](image/domino.jpg)

## Components

- **Domino**
  - **Problem:** such as OS-upgrade, involves many interdependent conditions & tasks.
  - Each condition is a domino tile, and their dependencies form a DAG.
  - A condition satisfied = a tile falls → auto-broadcasts to downstream (domino).
  - A task can stick to 1 tile - when the tile falls, auto-execute the task.
  - **Result:** improved eNB-base-station-upgrade $\color{blue}{\textsf{from}\sim9\textsf{min to}\sim1\textsf{min}}$.

- **ObjAnywhere:**

- **MsgSelf** is a priority FIFO queue.

- **SafePtr** is safer than std::shared_ptr.

- **SmartLog**

- **ThreadBack:** time-cost event running in a separate thread → callback main thread.

## Engineering

- **[`ut/`](ut) each UT case is an end-to-end scenario.**
- [GitHub Actions](https://github.com/domino-library/domino-library/actions/workflows/ci.yml)
  runs GoogleTest under Valgrind on pushes, pull requests, and a weekly schedule.
- [High line & branch coverage](ut/gcovr/gcovr.cfg).
- The currently verified CI environment is **Ubuntu + GCC**.

## Build and Run

Requirements: CMake 3.16 or newer and a C++17-capable compiler.

Ask AI for details.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## Maintainer

Primary maintainer: [fchn289](https://github.com/fchn289)  
Contribution history: [GitHub contributors](https://github.com/domino-library/domino-library/graphs/contributors)  
Contact: fchn289@gmail.com
