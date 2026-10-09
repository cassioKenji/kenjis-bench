# kenjis-bench

Projetos de bancada com microcontroladores e placas: ESP32, Raspberry Pi Pico, Raspberry Pi, etc.

| Pasta | O que é |
|---|---|
| [weather-monitor](weather-monitor/) | Clima, qualidade do ar e relógio num LCD 20x4, com repetidor Wi-Fi (ESP32-S3) |
| [playground](playground/) | Experimentos e testes rápidos |
| [tools](tools/) | Scripts compartilhados entre os projetos |

## Ferramentas

- **`tools/usb_attach.py`:** conecta uma placa ao WSL via `usbipd`. Detecta a placa pelo VID:PID, faz o bind (pedindo permissão de administrador) e o attach, e espera a porta `/dev/ttyUSB*` ou `/dev/ttyACM*` aparecer. Use `--watch` para reconectar automaticamente após cada reset.
- **`tools/pio_usb_attach.py`:** hook do PlatformIO que roda o `usb_attach.py` antes de upload e monitor. Para usar num projeto, adicione ao `platformio.ini`:
  ```ini
  extra_scripts = pre:../tools/pio_usb_attach.py
  ```

Credenciais ficam em `include/secrets.h` de cada projeto, que está no `.gitignore`. Cada projeto traz um `secrets.example.h` como modelo.
