# Tasks — wkview-m0-toolchain

- [x] Owner (elevated PowerShell): install VS2022 C++ workload via choco command in proposal
- [x] Owner: paste `cl` version output + confirm `vcvarsall.bat` exists
- [x] Agent: `vcpkg install icu --triplet x64-windows`
- [x] Agent: verify complete VS instance (vswhere + MSVC 14.44.35207 present; `--system-information` is not a build-jsc flag — superseded by this check)
- [ ] Agent: archive change, open `wkview-m1-host-jsc` proposal
