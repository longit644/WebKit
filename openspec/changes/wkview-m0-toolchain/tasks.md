# Tasks — wkview-m0-toolchain

- [ ] Owner (elevated PowerShell): install VS2022 C++ workload via choco command in proposal
- [ ] Owner: paste `cl` version output + confirm `vcvarsall.bat` exists
- [ ] Agent: `vcpkg install icu --triplet x64-windows`
- [ ] Agent: re-run `build-jsc --system-information`, confirm no nmake failure
- [ ] Agent: archive change, open `wkview-m1-host-jsc` proposal
