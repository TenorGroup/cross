"""Honor configured C3 LTO after pioarduino installs its linker defaults."""


def configure_c3_lto(env):
    if env.BoardConfig().get("build.mcu") != "esp32c3":
        return
    configured = env.ParseFlags(env.GetProjectOption("build_flags", ""))
    if "-flto=4" not in [str(flag) for flag in configured.get("CCFLAGS", [])]:
        return
    flags = [flag for flag in env.get("LINKFLAGS", []) if str(flag) != "-fno-lto"]
    if "-flto=4" not in [str(flag) for flag in flags]:
        flags.append("-flto=4")
    env.Replace(LINKFLAGS=flags)


Import("env")  # noqa: F821 -- provided by PlatformIO
configure_c3_lto(env)  # noqa: F821
