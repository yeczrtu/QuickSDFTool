# Contributing to QuickSDFTool

Thanks for helping improve QuickSDFTool. Contributions are most useful when they
stay focused on the UE editor painting workflow, SDF threshold-map generation,
documentation, sample content, or release verification.

## Development Setup

1. Copy this repository into a C++ Unreal Engine project as
   `Plugins/QuickSDFTool/`.
2. Regenerate project files.
3. Build the host project from Visual Studio or your normal Unreal build
   workflow.
4. Enable the `QuickSDFTool` plugin and restart the editor.
5. Verify that the `Quick SDF` editor mode appears and can be opened.

QuickSDFTool v1.0 is release-verified on Unreal Engine 5.7.x. Compatibility
work for newer Unreal versions is welcome, but please include the exact engine
version and build environment used for verification.

## Good Contribution Areas

- Documentation clarifications, screenshots, examples, and troubleshooting
  notes.
- UE version verification and small compatibility fixes.
- Focused workflow fixes for painting, material-slot handling, mask import or
  export, SDF generation, and generated texture output.
- Sample content that makes the plugin easier to evaluate.

## Coding Guidelines

- Keep changes scoped to one issue or workflow at a time.
- Follow the existing Unreal Engine C++ style in the surrounding files.
- Prefer project-local helper types and existing module boundaries over new
  broad abstractions.
- Avoid unrelated formatting churn.
- Include comments only where they explain non-obvious behavior or Unreal API
  constraints.
- Do not commit generated project files, local build products, or Unreal cache
  directories.

## Reporting and Reproducing Issues

When filing an issue or opening a pull request for a fix, include:

- Unreal Engine version.
- QuickSDFTool version, release tag, or commit.
- Operating system and compiler or IDE version.
- A minimal reproduction path.
- Expected behavior and actual behavior.
- Logs, screenshots, or short videos when they help explain the issue.

For build issues, include the first relevant compiler error and the module that
failed. For rendering or paint workflow issues, include the mesh type, material
slot setup, texture size, and any imported mask details that affect the result.

## Pull Requests

1. Fork the repository and create a focused branch.
2. Keep source, content, and documentation changes separated when practical.
3. Update documentation when behavior or supported workflows change.
4. Run the relevant Unreal build or editor verification before requesting
   review.
5. Fill out the pull request template, including verification notes and any
   compatibility impact.

Maintainers may ask for a smaller scope, additional reproduction details, or
more verification before merging.

## Security

Do not report security vulnerabilities in public issues. Follow
[SECURITY.md](./SECURITY.md) for private vulnerability reporting.
