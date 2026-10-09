# Hook do PlatformIO: conecta a placa ao WSL antes de upload/monitor.
# Uso no platformio.ini de um projeto (um nível abaixo da raiz do repositório):
#   extra_scripts = pre:../tools/pio_usb_attach.py
import os
import platform
import subprocess
import sys

from SCons.Script import COMMAND_LINE_TARGETS

Import("env")

on_wsl = "microsoft" in platform.release().lower()

if on_wsl and {"upload", "monitor", "uploadfs"} & set(COMMAND_LINE_TARGETS):
    script = os.path.join(env.subst("$PROJECT_DIR"), "..", "tools", "usb_attach.py")
    if subprocess.run([sys.executable, script]).returncode != 0:
        env.Exit(1)
