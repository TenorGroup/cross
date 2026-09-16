# Development workflow

1. Fork the repository and create a focused branch from `main`.
2. Keep changes scoped to one fix or feature. Discuss larger work with the maintainers.
3. Follow the build and host-test commands in the root README. Use `./bin/clang-format-fix` for C/C++ formatting.
4. Open a pull request against `main`, describing the problem, the change and the checks you ran.
5. Include device and orientation details for hardware reports. Share synthetic books or minimal examples that you have permission to distribute.

Keep private device data, credentials, logs and books out of commits. The root ignore file excludes common local outputs.
