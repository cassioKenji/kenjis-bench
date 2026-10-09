#!/usr/bin/env python3
"""Conecta a placa ESP32 ao WSL via usbipd-win.

Detecta a placa pelo VID:PID, faz o bind (pedindo UAC) se ainda não foi feito,
faz o attach e espera a porta serial aparecer em /dev.

Uso:
    scripts/usb_attach.py            # attach uma vez
    scripts/usb_attach.py --watch    # mantém reconectando (útil na porta USB nativa)
    scripts/usb_attach.py --busid 1-5
"""
import argparse
import glob
import json
import re
import subprocess
import sys
import time

# VID:PID dos conversores USB-serial comuns em placas ESP32
KNOWN_IDS = {
    "0403:6001": "FTDI FT232R",
    "0403:6015": "FTDI FT231X",
    "10c4:ea60": "Silicon Labs CP210x",
    "1a86:55d3": "WCH CH343",
    "1a86:55d4": "WCH CH9102",
    "1a86:7523": "WCH CH340",
    "303a:1001": "ESP32-S3 USB nativa (JTAG/serial)",
}

USBIPD = "usbipd.exe"


def fail(msg):
    print(f"[usb_attach] erro: {msg}", file=sys.stderr)
    sys.exit(1)


def log(msg):
    print(f"[usb_attach] {msg}")


def usbipd_state():
    try:
        out = subprocess.run([USBIPD, "state"], capture_output=True, check=True).stdout
    except FileNotFoundError:
        fail("usbipd.exe não encontrado. Instale no Windows com: winget install usbipd")
    except subprocess.CalledProcessError as e:
        fail(f"'usbipd state' falhou: {e.stderr.decode(errors='replace').strip()}")
    return json.loads(out.decode("utf-8", errors="replace"))["Devices"]


def vid_pid(device):
    m = re.search(r"VID_([0-9A-F]{4})&PID_([0-9A-F]{4})", device.get("InstanceId") or "", re.I)
    return f"{m.group(1)}:{m.group(2)}".lower() if m else None


def find_board(busid):
    devices = usbipd_state()
    if busid:
        for d in devices:
            if d["BusId"] == busid:
                return d
        fail(f"nenhum dispositivo no BUSID {busid}")
    boards = [d for d in devices if d["BusId"] and vid_pid(d) in KNOWN_IDS]
    if not boards:
        fail("placa não encontrada. Confira o cabo (precisa ser de dados) e rode 'usbipd.exe list'.")
    if len(boards) > 1:
        log("mais de uma placa encontrada, usando a primeira (use --busid para escolher):")
        for d in boards:
            log(f"  {d['BusId']}  {vid_pid(d)}  {d['Description']}")
    return boards[0]


def serial_ports():
    return set(glob.glob("/dev/ttyUSB*") + glob.glob("/dev/ttyACM*"))


def bind(busid):
    log(f"fazendo bind de {busid} (aceite o prompt de administrador no Windows)...")
    ps = (
        f"$p = Start-Process usbipd -ArgumentList 'bind','--busid','{busid}' "
        "-Verb RunAs -WindowStyle Hidden -Wait -PassThru; exit $p.ExitCode"
    )
    if subprocess.run(["powershell.exe", "-NoProfile", "-Command", ps]).returncode != 0:
        fail("bind falhou ou foi cancelado")


def wait_for_port(before, timeout=10):
    end = time.time() + timeout
    while time.time() < end:
        new = serial_ports() - before
        if new:
            return sorted(new)[0]
        time.sleep(0.3)
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--busid", help="BUSID específico (padrão: detecta pelo VID:PID)")
    ap.add_argument("--watch", action="store_true", help="fica rodando e reconecta após cada reset/replug")
    args = ap.parse_args()

    board = find_board(args.busid)
    busid = board["BusId"]
    log(f"placa: {busid}  {vid_pid(board)}  {KNOWN_IDS.get(vid_pid(board), board['Description'])}")

    if not board["PersistedGuid"]:
        bind(busid)

    if args.watch:
        log("modo --watch: Ctrl+C para sair")
        sys.exit(subprocess.run([USBIPD, "attach", "--wsl", "--busid", busid, "--auto-attach"]).returncode)

    if board["ClientIPAddress"]:
        ports = sorted(serial_ports())
        log(f"já conectada ao WSL ({', '.join(ports) or 'porta serial ainda não apareceu'})")
        return

    before = serial_ports()
    r = subprocess.run([USBIPD, "attach", "--wsl", "--busid", busid], capture_output=True, text=True)
    if r.returncode != 0:
        fail(f"attach falhou: {(r.stderr or r.stdout).strip()}")

    port = wait_for_port(before)
    if not port:
        fail("attach feito, mas nenhuma porta /dev/ttyUSB* ou /dev/ttyACM* apareceu")
    log(f"pronto: {port}")


if __name__ == "__main__":
    main()
