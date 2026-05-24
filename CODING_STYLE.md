# wfview C++ Style Guide

This guide applies to wfview-owned C++ and QML code. Do not reformat bundled or imported libraries such as `qdarkstyle/`, `old-source/`, generated build trees, or third-party DSP/audio sources unless the change is required for integration.

## Scope

- Apply these rules to files under `include/`, `src/`, and `qml/` that are maintained by wfview.
- Prefer small style cleanup patches near functional changes.
- Avoid whole-tree formatting commits until the team agrees on the churn.

## Formatting

- Use 4 spaces for indentation; do not use tabs.
- Keep lines readable, preferably under 120 columns.
- Put opening braces on their own line for classes, functions, namespaces, and control blocks to match the existing Qt-heavy code.
- Use one blank line between logical blocks. Avoid large vertical gaps.
- Prefer early returns over deep nesting.
- Keep comments short and useful. Do not restate the code.

## Files and Includes

- Use `.h` and `.cpp` for C++ files.
- Use `#pragma once` for new headers. Existing include guards may be converted when touching a header substantially.
- Include order in `.cpp` files:
  1. the matching header,
  2. wfview headers,
  3. Qt and third-party headers,
  4. C++ standard headers.
- Include order in `.h` files:
  1. direct base-class or required wfview headers,
  2. Qt and third-party headers,
  3. standard headers,
  4. forward declarations where possible.
- Do not put `using namespace` in headers.

## Naming

- Keep existing public API names unless there is a clear migration reason.
- Classes and QML component types use `UpperCamelCase`.
- Functions and variables use the existing subsystem convention. New Qt controller code should prefer `lowerCamelCase`.
- Private data members should use the existing class or subsystem convention. Do not introduce prefixes solely for style reasons.
- Constants should have descriptive names and avoid magic numbers in call sites.

## C++ Practices

- Use C++17 and Qt idioms already present in the project.
- Prefer RAII and Qt parent ownership over manual cleanup.
- Mark single-argument constructors `explicit`.
- Use `const` whenever it clarifies ownership or prevents accidental mutation.
- Prefer references for required objects and pointers for nullable objects.
- Check pointer and index validity before dereferencing.
- Keep signal/slot connections close to the object setup they belong to.
- Avoid adding global state. Put file-local helpers in an unnamed namespace.

## Qt and QML

- Expose QML data through small controller/model APIs rather than ad hoc context properties.
- Keep QML bindings simple. Move complex behavior into C++ or small QML helper functions.
- Use `required property` in delegates when a model role is mandatory.
- Keep palette and theme behavior inherited unless a control represents a real device color or state.

## Formatting Tool

Use the repository `.clang-format` for C++ files when touching code:

Linux/macOS:

```sh
clang-format -i include/ControllerController.h src/ControllerController.cpp
```

Windows with clang-format on `PATH`:

```powershell
clang-format -i include\ControllerController.h src\ControllerController.cpp
```

Windows with Qt Creator's bundled clang-format:

```powershell
C:\Qt\Tools\QtCreator\bin\clang\bin\clang-format.exe -i include\ControllerController.h src\ControllerController.cpp
```

Only run it on wfview-owned files. Do not run it on build directories or imported libraries.

## Unit Tests

- Put Qt Test cases under `tests/`.
- Prefer tests for pure controller/model logic that does not need a radio, audio device, or USB hardware.
- Use synthetic fixtures for rig, settings, and controller data.
- Keep tests deterministic and safe to run in CI.
