"""Translate pioarduino's esptool v5 flags for pinned esptool v4 on Windows."""

Import("env")

REPLACEMENTS = (
    ("write-flash", "write_flash"),
    ("erase-flash", "erase_flash"),
    ("--flash-mode", "--flash_mode"),
    ("--flash-freq", "--flash_freq"),
    ("--flash-size", "--flash_size"),
    ("no-reset-no-sync", "no_reset_no_sync"),
    ("no-reset-stub", "no_reset_stub"),
    ("default-reset", "default_reset"),
    ("usb-reset", "usb_reset"),
    ("hard-reset", "hard_reset"),
    ("soft-reset", "soft_reset"),
    ("no-reset", "no_reset"),
)


def adapt(value):
    for newer, older in REPLACEMENTS:
        value = value.replace(newer, older)
    return value


if "_ORIGINAL_VERBOSE_ACTION" not in env:
    env["_ORIGINAL_VERBOSE_ACTION"] = env.VerboseAction

    def compatible_verbose_action(self, action, *args, **kwargs):
        if isinstance(action, str):
            action = adapt(action)
        elif isinstance(action, list):
            action = [adapt(item) if isinstance(item, str) else item for item in action]
        return self["_ORIGINAL_VERBOSE_ACTION"](action, *args, **kwargs)

    env.AddMethod(compatible_verbose_action, "VerboseAction")

builders = env.get("BUILDERS", {})
if "ElfToBin" in builders:
    action = getattr(builders["ElfToBin"], "action", None)
    command = getattr(action, "cmd_list", None)
    if isinstance(command, str):
        action.cmd_list = adapt(command)

for key in ("ERASECMD", "UPLOADCMD"):
    if isinstance(env.get(key), str):
        env[key] = adapt(env[key])

for key in ("ERASEFLAGS", "UPLOADERFLAGS"):
    if isinstance(env.get(key), list):
        env[key] = [adapt(item) if isinstance(item, str) else item for item in env[key]]
