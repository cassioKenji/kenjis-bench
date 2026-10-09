# weather-monitor

Estação de clima para casa num LCD 20x4, com ESP32-S3. Também funciona como repetidor Wi-Fi.

O LCD alterna entre duas telas:

```
S. Parnaíba    10:42        Casa: MINHA-REDE
30.3°C 💧50% ☂0%            192.168.15.26 -52dB
↑05:40 ↓18:09 UV8           AP: ESP32-S3 (WPA2)
≋~6km/h Ar 149 Sens         Clientes: 1
        clima (60 s)                status (5 s)
```

- **Clima:** temperatura, umidade, chance de chuva, vento, nascer e pôr do sol, e índice UV. Os dados vêm da [Open-Meteo](https://open-meteo.com) e são atualizados a cada 10 min.
- **Qualidade do ar:** índice americano (US AQI), também da Open-Meteo.
- **Relógio:** acertado pela internet (NTP), no horário de Brasília.
- **Repetidor:** conecta na rede de casa e cria o AP `ESP32-S3` (WPA2). Quem se conecta nele acessa a internet através do ESP32 (NAT).
- **LED RGB da placa:** pisca verde enquanto houver alguém conectado no AP.

## Hardware

- ESP32-S3 DevKitC-1 N16R8
- LCD 20x4 HD44780 com adaptador I2C (PCF8574, endereço 0x27 ou 0x3F, detectado sozinho)

| Adaptador I2C | ESP32-S3 |
|---|---|
| SDA | GPIO 9 |
| SCL | GPIO 8 |
| VCC | 5V (com 3V3 o texto pode ficar invisível) |
| GND | GND |

Se a tela mostrar só quadradinhos, o LCD está sem comunicação: confira os fios do SDA e do SCL. Se acender sem mostrar texto, ajuste o contraste no potenciômetro azul do adaptador.

## Configuração

1. Copie `include/secrets.example.h` para `include/secrets.h` e preencha o Wi-Fi de casa e a senha do AP. O `secrets.h` fica fora do git.
2. Ajuste a localização no topo de `src/main.cpp`: `LATITUDE` e `LONGITUDE`, e o nome da cidade em `drawWeather()`.
3. Os tempos de cada tela ficam em `STATUS_PAGE_MS` e `WEATHER_PAGE_MS`.

## Gravar

Com [PlatformIO](https://platformio.org), dentro desta pasta:

```bash
pio run -t upload -t monitor
```

No WSL, o hook `../tools/pio_usb_attach.py` conecta a placa ao Linux via `usbipd` antes de gravar.
