# ADR 0003 — Qt & C++ baseline

**Status:** accepted

The critique flagged "Qt 6.10" as possibly unpublished. **Resolved:** Qt **6.10.3** (and 6.11.1) are
installed locally and build this skeleton, so **6.10 is the real minimum**:
`find_package(Qt6 6.10 REQUIRED ...)`.

**Module note (Milestone A).** The BLE scanner needs Qt Bluetooth (`qtconnectivity`). The local
6.10.3 kit was installed **without** that module, so the build is verified on **6.11.1**; CI
installs `qtconnectivity` for 6.10.3. `find_package(Qt6 6.10 REQUIRED COMPONENTS … Bluetooth)` keeps
6.10 as the minimum. (Also verified: the Qt Win32 backend does not deliver advertisement updates
after discovery — live values on Windows will need the headless probe.)

**C++23.** `CMAKE_CXX_STANDARD 23`. Verified locally: GCC 13.3 compiles `-std=c++23` with
`<expected>` and `<optional>`. `std::expected` is the intended error-handling backbone.

**Feature-gating.** `std::flat_map` and `std::print` are late/uneven in libc++ and the Android NDK
r28 libc++ — gate behind `__cpp_lib_*` with fallbacks (`QHash` / `qDebug`). **Still unverified
off-Linux:** pin exact minimum compilers and re-verify `std::expected` / `std::flat_map` in CI on
**MSVC 2022 / Apple Clang / Android NDK** before leaning on them (an early CI spike).

**Charts.** Deferred — none yet. Locally `Qt6::Charts` is present, `Qt6::Graphs` is **not**;
confirm availability before choosing, and isolate behind a chart view-model so the choice is
swappable.
