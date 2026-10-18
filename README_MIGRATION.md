# Abarth Dashboard - Migration & Setup Guide

Questo file è stato generato per salvare le istruzioni discusse nella chat riguardo il passaggio da PlatformIO/Arduino a ESP-IDF nativo, e per spiegare come configurare il progetto su un nuovo computer (es. MacBook).

## 1. Trasferire il Progetto sul Mac
Essendo passati a ESP-IDF, l'unico vero requisito per il codice è la cartella `abarth-dashboard` che contiene il codice sorgente (i file in `main/`, `CMakeLists.txt`, `sdkconfig.defaults`, ecc.).
Puoi copiare l'intera cartella tramite chiavetta USB, Airdrop o caricandola su GitHub. 
*Nota: la cartella `build/` può essere eliminata prima di copiare, per alleggerire il peso, in quanto verrà rigenerata dal Mac.*

## 2. Installazione Prerequisiti su Mac (Terminale)
1. Installa **Homebrew** (se non lo hai già) incollando questo comando nel Terminale:
   ```bash
   /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
   ```
2. Installa gli strumenti di compilazione:
   ```bash
   brew install cmake ninja dfu-util python3
   ```

## 3. Installazione dell'ambiente ESP-IDF (Versione 5.4)
Sempre dal Terminale, scarica e installa il framework ufficiale Espressif:
```bash
mkdir -p ~/esp
cd ~/esp
git clone -b v5.4 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32p4
```

## 4. Compilare e Caricare (Flashare) il codice dal Mac
1. Nel Terminale, spostati dentro la cartella del progetto:
   ```bash
   cd percorso/della/cartella/abarth-dashboard
   ```
2. Attiva l'ambiente ESP-IDF (questo va fatto **ogni volta** che apri un nuovo Terminale per programmare):
   ```bash
   source ~/esp/esp-idf/export.sh
   ```
3. Collega la dashboard (Guition JC1060P470C) via USB e lancia la compilazione e il caricamento:
   ```bash
   idf.py build
   idf.py flash monitor
   ```

*(Su Mac, le porte USB si chiamano in modo leggermente diverso rispetto a Linux, tipo `/dev/cu.usbmodem1101` anziché `/dev/ttyACM0`. Tuttavia, il comando `idf.py` solitamente riconosce la scheda in automatico).*

---

## Riepilogo delle modifiche effettuate
Durante le nostre sessioni, abbiamo fatto enormi passi in avanti per la stabilità della dashboard:
- **Migrazione a ESP-IDF**: Abbandonato Arduino in favore del framework nativo per risolvere i riavvii continui dovuti a un consumo anomalo della memoria.
- **Grafica Fluida (LVGL Mode 0)**: Abbiamo spostato il buffer di disegno dalla memoria lenta (PSRAM) alla memoria ultraveloce (SRAM interna), aumentandolo a 60 linee orizzontali. Questo garantisce oltre 60 FPS e un carico della CPU bassissimo (4%), mantenendo un sistema privo di blocchi (accettando un microscopico difetto visivo/tearing durante lo scorrimento veloce in cambio dell'enorme aumento di stabilità).
- **Nuove Funzionalità Settings**: Aggiunta logica per gestire la connessione WiFi (SSID, IP, Gateway, DNS) e la connessione OBD via BLE (MAC Address), compreso il blocco intelligente di scansioni parallele e il pulsante per disconnettersi.
- **Bootloader e Crash**: Risolto il problema del bootloop "Brownout/Reset" e corretto il caricatore del firmware a 16MB.
