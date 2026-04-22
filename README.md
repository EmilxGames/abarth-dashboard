# Abarth 595 Dashboard

Dashboard touchscreen per **Abarth 595 (2021)** che legge i sensori della
centralina tramite adattatore OBD2 Bluetooth (**Vgate vLinker FD**) e li
mostra in tempo reale su un display **Guition JC1060P470C-I-W-Y**
(ESP32-P4 + ESP32-C6, LCD 7" 1024x600 MIPI-DSI, touch capacitivo GT911).

> ⚠️ Il progetto si collega solo in **lettura**: non scrive nulla sulla
> centralina, non "remappa" l'auto e non interagisce col bus CAN in modo
> invasivo. Si parla all'auto solo attraverso la porta OBD standard.

## Funzionalità

- Connessione automatica via **Bluetooth LE** al dongle vLinker FD (Vgate).
- Inizializzazione ELM327 + polling ciclico dei PID.
- Interfaccia LVGL 9 a schede con valori aggiornati in tempo reale:
  - **Motore**: giri, velocità, carico, acceleratore
  - **Temperature**: liquido, olio motore, aria aspirata, temp. esterna, gas di scarico
  - **Pressioni**: boost turbo, MAP, pressione olio
  - **Elettrico**: tensione batteria, miscela (AFR / Lambda)
  - **Stato**: connessione dongle, contatori OK / errori
- PID **standard OBD-II Mode 01** (cross-car).
- PID **Mode 22 specifici FIAT/Abarth** per temperatura olio, boost reale,
  pressione olio — facilmente modificabili in
  [`src/obd/pids.cpp`](src/obd/pids.cpp).

## Hardware necessario

| Componente              | Modello                                   | Note                                      |
| ----------------------- | ----------------------------------------- | ----------------------------------------- |
| Display                 | Guition **JC1060P470C-I-W-Y** (ESP32-P4) | 7" 1024x600 touch, già con Wi-Fi/BT via C6 |
| Dongle OBD              | Vgate **vLinker FD**                      | Modalità BLE 4.0                          |
| Alimentazione           | USB-C 5V ≥ 1A (cavo OBD→USB-C opzionale) | Oppure cavo splitter OBD 12V → USB-C      |
| Cavo USB-C dati         | USB-C ↔ USB-A                             | Per flashare la prima volta               |

Nessun cablaggio invasivo: il vLinker FD va nella porta OBD2, il display
ovunque in abitacolo.

## Setup ambiente di sviluppo

Serve Python 3.9+ e PlatformIO.

```bash
pip install -U platformio
```

Clona e compila:

```bash
git clone https://github.com/EmilxGames/abarth-dashboard.git
cd abarth-dashboard
pio run                         # build env di default = abarth-p4-guition-custom
pio run --target upload         # flash del display (USB-C collegato)
pio device monitor              # log seriale (115200)
```

Su Windows, se `python` apre il Microsoft Store invece dell'interprete, disabilita
l'alias da *Impostazioni → App → Impostazioni app avanzate → Alias di esecuzione
dell'app* e installa Python ≥ 3.10 da <https://python.org> spuntando
"Add python.exe to PATH".

### Environment PlatformIO disponibili

| env                          | uso                                                                 |
| ---------------------------- | ------------------------------------------------------------------- |
| `abarth-p4-guition-custom`   | **default**: pin + init sequence JD9165 del Guition JC1060P470C-I-W-Y |
| `abarth-p4-funcboard`        | fallback sulla Function-EV-Board Espressif (pin generici, solo dev)  |

Per compilare esplicitamente un env specifico:

```bash
pio run -e abarth-p4-guition-custom
pio run -e abarth-p4-guition-custom --target upload
```

Il primo `pio run` scarica automaticamente:

- `platform-espressif32` (fork **pioarduino**, supporta ESP32-P4)
- core **arduino-esp32 v3.x**
- librerie: `lvgl`, `ESP32_Display_Panel`, `NimBLE-Arduino`

## Come funziona la connessione al dongle

1. Al boot il firmware inizializza NimBLE e fa una scansione di 10 s
   cercando un dispositivo chiamato `vLinker FD` (configurabile in
   [`src/config/app_config.h`](src/config/app_config.h)) o che espone il
   servizio GATT `0xFFF0`.
2. Trovato il dongle, si connette e sottoscrive le notifiche sulla
   caratteristica `0xFFF1` (RX dal dongle) e scrive comandi su `0xFFF2`
   (TX verso il dongle). Questa è la configurazione standard dei dongle
   ELM327 BLE cinesi "compatibili iPhone", inclusi vLinker FD, OBDLink CX
   e la maggior parte dei cloni.
3. Invia la sequenza di init `ATZ / ATE0 / ATL0 / ATS0 / ATH0 / ATSP0 /
   ATAT1 / ATST96` per resettare il dongle, disattivare echo, spazi e
   header, e attivare protocollo automatico + adaptive timing.
4. Entra nel loop di polling: interroga ciclicamente i PID configurati
   in [`src/obd/pids.cpp`](src/obd/pids.cpp) e salva i risultati decodificati
   nel data store.

Se il tuo dongle non viene trovato, controlla con [nRF Connect](https://www.nordicsemi.com/Products/Development-tools/nrf-connect-for-mobile)
i suoi UUID reali e aggiornali in `app_config.h`.

## Aggiungere / correggere PID Mode 22 Abarth

I PID Mode 22 (identifier 16 bit) variano a seconda della versione di
centralina. I valori di default sono quelli **più comunemente funzionanti**
sulla Abarth 595 1.4 T-Jet MultiAir, ma **verifica sulla tua auto** con
[Car Scanner](https://www.carscanner.info/) o FiatECUScan.

Apri [`src/obd/pids.cpp`](src/obd/pids.cpp), cerca la tabella
`kAbarthCustomPids`, e modifica i campi `request` (es. `"22706E"`) e
`pid_id` (es. `0x706E`). Se una formula non dà valori realistici,
modifica il decoder corrispondente (`dec_fiat_oil_temp`, `dec_fiat_boost_rel_bar`,
...).

Alcune fonti utili di PID per Abarth/FIAT 1.4 T-Jet:

- ScanGauge X-Gauge FIAT 500 Abarth: <https://scangauge.com/x-gauge-commands/fiat-500-abarth>
- Car Scanner community plugins per "FIAT / Alfa / Lancia" (cercabili
  dal menu "Plugins")
- Alfa 4C / Giulietta / 500 Abarth forum (le centraline MM10JF sono molto
  simili).

Se un PID risponde `NO DATA` il sistema lo marca come "non supportato" e
il valore sullo schermo resta `—` finché non arriva una lettura valida.

## Struttura del codice

```
abarth-dashboard/
├── platformio.ini
├── include/
│   └── lv_conf.h          # configurazione LVGL 9
├── src/
│   ├── main.cpp           # entry point (setup/loop)
│   ├── config/
│   │   └── app_config.h   # parametri modificabili
│   ├── data/
│   │   ├── data_store.h   # cache thread-safe valori
│   │   └── data_store.cpp
│   ├── display/
│   │   ├── board.h        # init MIPI-DSI + GT911 tramite ESP32_Display_Panel
│   │   └── board.cpp
│   ├── obd/
│   │   ├── ble_stream.h   # NimBLE wrapper stream-like
│   │   ├── ble_stream.cpp
│   │   ├── elm327.h       # client ELM327 (AT + OBD)
│   │   ├── elm327.cpp
│   │   ├── pids.h         # tabelle PID + decoder
│   │   ├── pids.cpp
│   │   ├── obd_manager.h  # state machine + task polling
│   │   └── obd_manager.cpp
│   └── ui/
│       ├── ui.h           # costruzione interfaccia LVGL
│       └── ui.cpp
├── scripts/
│   └── pre_build.py
└── README.md
```

## Troubleshooting

- **Il display resta nero / non carica LVGL**: stai probabilmente usando
  l'env fallback `abarth-p4-funcboard` (pin della Function-EV-Board
  Espressif, incompatibili col Guition reale). Passa a
  `pio run -e abarth-p4-guition-custom --target upload`. Il file
  [`src/esp_panel_board_custom_conf.h`](src/esp_panel_board_custom_conf.h)
  contiene i pin del Guition JC1060P470C-I-W-Y estratti dallo schematico
  V1.0 e dalla demo ufficiale `Demo_Arduino/lvgl_demo_v8`: LCD reset
  GPIO5, I2C touch SDA/SCL GPIO7/8, backlight PWM GPIO23. La init
  sequence e' quella del driver `esp_lcd_jd9165` ufficiale Guition.
- **Il dongle non viene trovato**: il vLinker FD ha due modalità (dip
  switch/pulsante): **AUTO** e **iPhone (BLE)**. Mettilo in modalità
  iPhone/BLE. Su ESP32-C6 il Bluetooth Classic non è disponibile, serve
  per forza BLE.
- **`NO DATA` ovunque**: il motore deve essere acceso e la chiave in
  posizione ON. Alcuni PID Mode 01 (es. ambient air temp) non sono
  riportati da FIAT: resteranno `—`. I Mode 22 richiedono che i PID id
  siano giusti per la tua centralina.
- **Valori assurdi (es. boost 300 bar)**: la formula del decoder non
  corrisponde alla codifica reale della centralina. Edita il decoder
  corrispondente in `src/obd/pids.cpp`.

## Sicurezza

Il firmware è solo lettura (OBD Mode 01 + Mode 22 "read data by local
identifier"): non invia comandi scrittura (Mode 2E) né aziona attuatori
(Mode 04, 09). Tuttavia l'uso in strada deve rispettare il codice: monta
il display in modo che non distragga e non occluda la visuale.

## Licenza

MIT — vedi [LICENSE](LICENSE).
