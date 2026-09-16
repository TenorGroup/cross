"""Keep local build paths out of diagnostics embedded in distributable binaries."""
Import("env")

# SCons passes these as individual arguments, including when a path has spaces.
for variable, label in (("PROJECT_DIR", "."), ("PROJECT_PACKAGES_DIR", "packages")):
    source = env.subst("$" + variable)
    if source and not source.startswith("$"):
        env.Append(CCFLAGS=["-ffile-prefix-map=" + source + "=" + label])
