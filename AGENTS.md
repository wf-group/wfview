# Repository Guidelines

## Project Structure & Module Organization

wfview is a Qt/qmake C++17 application. Main sources live in `src/`, with matching headers in `include/`. Radio protocol implementations are under `src/radio/`; audio handlers, processors, resamplers, and bundled DSP code are under `src/audio/`. Qt Designer UI files sit beside implementations in `src/*.ui`. Static assets and install metadata are in `resources/`, rig definitions in `rigs/`, translations in `translations/`, and Linux service files in `systemd/`. The primary qmake projects are `wfview.pro` for the GUI and `wfserver.pro` for the server.

## Build, Test, and Development Commands

Use an out-of-tree build directory so generated Makefiles and objects stay out of source:

```sh
mkdir build
cd build
qmake ../wfview.pro
make -j
```

For Qt 6, use the platform qmake variant, for example `qmake6 ../wfview.pro`. Build the server with `qmake ../wfserver.pro && make -j`. Install only after a successful build with `sudo make install`. See `INSTALL.md` for distro-specific packages.

## Coding Style & Naming Conventions

Follow `CODING_STYLE.md` for wfview-owned C++ and QML. Use C++17, Qt containers and signal/slot patterns where they fit, and keep UI logic in the relevant controller or QML component. Do not reformat bundled libraries, `old-source/`, generated build trees, or third-party DSP/audio code. Add new files to the appropriate `SOURCES`, `HEADERS`, `FORMS`, or `RESOURCES` section of the `.pro` file.

## Testing Guidelines

Unit tests live in `tests/` and use Qt Test. Build them out of tree with `qmake ../tests/tests.pro && make check`, or on Windows generate the Makefile with the matching Qt qmake and run `jom check`. Treat a clean qmake build as the minimum validation, then manually exercise affected hardware/UI workflows that tests cannot cover yet.

## Commit & Pull Request Guidelines

Recent history uses short, descriptive commit subjects, for example `Fix for MSVC compilers` or `Additional wfserver documentation`. Keep subjects imperative or clearly descriptive, and mention the affected subsystem when useful. Pull requests should describe the behavior change, list tested platforms or commands, link related issues, and include screenshots for visible UI changes.

## Security & Configuration Tips

Do not commit local build products, generated Makefiles, radio credentials, logs, or machine-specific Qt Creator settings. Keep generated translation/resource updates intentional and reviewable. When touching network, radio control, or audio paths, prefer conservative defaults and document any user-visible configuration changes.
