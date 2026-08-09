# Contributing

Thanks for taking an interest. This is a small Windows utility; the guidelines
below are what keeps it buildable, tested, and reviewable.

## Building

Windows with the MSVC toolchain and CMake 3.21+:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The x86 build and the installer are described in the README under *Development*.

## Reporting

- **Bugs and feature requests**: open a GitHub issue.
- **Security vulnerabilities**: do *not* open a public issue. Follow
  [`SECURITY.md`](SECURITY.md) — private reporting through GitHub Security
  Advisories.

## Making changes

- Branch from `main` and open a pull request. `main` is protected: a PR is
  required and the CI checks must pass before it can merge.
- **Bump the version** in `CMakeLists.txt` (`project(... VERSION x.y.z)`) in
  every PR. CI enforces this; the release workflow refuses a tag that disagrees
  with it.
- **Add tests for new behaviour.** Non-trivial logic lives behind a testable
  seam and has a suite under `tests/` (see the existing `rules`, `presets`,
  `schedule`, and `diagnostic` suites). A change to parsing, rule matching, or
  the retry schedule should come with the test that pins it.
- Keep the build warning-clean: CI compiles with `/W4 /permissive-` and
  `-DCMAKE_COMPILE_WARNING_AS_ERROR=ON`, and CodeQL and MSVC analysis run on
  every PR.
- Match the style of the surrounding code — comment density, naming, and the
  habit of explaining *why* rather than *what*. `docs/design.md` records the
  rationale and the approaches that were tried and rejected; read it before
  changing behaviour, and update it when the reasoning changes.

## License

By contributing you agree that your contributions are licensed under the
project's [MIT license](LICENSE).
