#pragma once

/**
 * @file settings.h
 * @brief Impostazioni utente persistenti (NVS / Preferences).
 *
 * Thread-safety: le API leggono/scrivono NVS tramite `Preferences`, che
 * e' tread-safe lato IDF. L'istanza globale fa caching in RAM per letture
 * veloci; le scritture vengono propagate immediatamente in NVS.
 */

#include <cstdint>
#include <string>
#include "data/data_store.h"

namespace abarth::settings {

/// Unita' di misura per le temperature mostrate in UI.
enum class TempUnit : uint8_t {
    Celsius = 0,
    Fahrenheit = 1,
};

/// Unita' di misura per la velocita'.
enum class SpeedUnit : uint8_t {
    KmH = 0,
    Mph = 1,
};

/// Unita' di misura per le pressioni (MAP, boost, carburante).
enum class PressureUnit : uint8_t {
    Kpa = 0,    ///< solo kPa
    Bar = 1,    ///< solo bar
    Both = 2,   ///< mostra entrambe (kPa sul valore, bar nell'unita')
};

/// Stile dell'interfaccia grafica.
enum class UiTheme : uint8_t {
    MinimalFunctional = 0, ///< Minimalista funzionale ad alta leggibilita'
};

/// Stile complessivo dell'interfaccia (scelta dell'utente, si applica al riavvio).
enum class UiStyle : uint8_t {
    Classic = 0,  ///< Interfaccia attuale (main/ui)
    Racing  = 1,  ///< Nuova interfaccia ad alte prestazioni (main/ui_v2)
};

/// Texture di sfondo dello schermo.
enum class BgTexture : uint8_t {
    Original = 0,
    CarbonImage = 1,
    // --- Nuovi sfondi (aggiunti, non sostituiscono i precedenti) ---
    CheckeredFlag = 2,   ///< Bandiera a scacchi racing
    ItalianFlag = 3,     ///< Tricolore italiano sfumato
    ScorpionWatermark = 4, ///< Scorpione Abarth watermark
    AsphaltTrack = 5,    ///< Texture asfalto da circuito
};

/// Orientamento dello schermo. La rotazione e' software (MIPI-DSI scansiona
/// sempre 1024x600): LVGL ruota il framebuffer a ogni flush. Il touch viene
/// trasformato di conseguenza. Applicato al boot (richiede reboot).
enum class ScreenOrientation : uint8_t {
    Landscape = 0,       ///< Orizzontale 1024x600 (default, nessuna rotazione)
    Portrait = 1,        ///< Verticale 600x1024 (rotazione 90 gradi)
    PortraitInverse = 2, ///< Verticale capovolto 600x1024 (rotazione 270 gradi)
};

/// Modalita' di collegamento alla vettura
enum class ConnectionMode : uint8_t {
    BleObd    = 0,  ///< Dongle Bluetooth LE (vLinker FD / ELM327) - default
    WifiObd   = 1,  ///< Dongle WiFi OBD (Socket TCP)
    DirectCan = 2,  ///< Connessione Diretta CAN-Bus (TWAI + WeAct ISOCANFD V1)
};

struct Settings {
    /// Luminosita' retroilluminazione in percentuale 10..100.
    uint8_t   brightness   = 80;
    /// Unita' temperatura preferita.
    TempUnit  temp_unit    = TempUnit::Celsius;
    /// Unita' velocita' preferita.
    SpeedUnit speed_unit   = SpeedUnit::KmH;
    /// Unita' pressione preferita (kPa / bar / entrambi).
    PressureUnit pressure_unit = PressureUnit::Bar;
    /// Stile grafico dell'interfaccia.
    UiTheme   ui_theme     = UiTheme::MinimalFunctional;
    UiStyle      ui_style     = UiStyle::Classic;  ///< Classico o nuova UI Racing
    /// Texture di sfondo.
    BgTexture bg_texture   = BgTexture::Original;
    /// Orientamento schermo (orizzontale/verticale/verticale capovolto).
    ScreenOrientation screen_orientation = ScreenOrientation::Landscape;
    /// Tema scuro attivo.
    bool      dark_theme   = true;

    /// Modalita' di collegamento alla vettura (BLE, WiFi, Diretto CAN)
    ConnectionMode connection_mode = ConnectionMode::BleObd;

    /// Parametri CAN-Bus Diretto (TWAI ESP32-P4 + WeAct ISOCANFD V1)
    /// Rete 1: C-CAN Powertrain (Alta velocita' 500k - TWAI0)
    bool      can1_enabled        = true;    ///< Abilita CAN 1 (C-CAN Motore)
    uint8_t   can1_tx_pin         = 20;      ///< GPIO TX TWAI0 (default GPIO 20)
    uint8_t   can1_rx_pin         = 21;      ///< GPIO RX TWAI0 (default GPIO 21)
    uint32_t  can1_baud_rate      = 500000;  ///< Baud rate (500 kbps C-CAN Abarth Motore)

    /// Rete 2: B-CAN Body (Bassa velocita' 50k - TWAI1)
    bool      can2_enabled        = true;    ///< Abilita CAN 2 (B-CAN Carrozzeria)
    uint8_t   can2_tx_pin         = 32;      ///< GPIO TX TWAI1 (default GPIO 32)
    uint8_t   can2_rx_pin         = 33;      ///< GPIO RX TWAI1 (default GPIO 33)
    uint32_t  can2_baud_rate      = 50000;   ///< Baud rate (50 kbps B-CAN Abarth Carrozzeria)

    bool      can_listen_only     = true;    ///< Se true, solo ascolto passivo (nessuna query attiva). Default sicuro: true

    /// Ricerca e connessione automatica OBD all'avvio
    bool      auto_connect = true;
    /// Modalita' simulazione / test con dati telemetrici sintetici
    bool      demo_mode    = false;
    /// Modalita' debug avanzata (sblocca la simulazione e mostra info nello splashscreen)
    bool      debug_mode   = false;
    /// MAC address salvato per il dongle BLE
    std::string saved_dongle_mac = "";
    /// SSID WiFi salvato
    std::string saved_wifi_ssid = "";
    /// Password WiFi salvata
    std::string saved_wifi_pass = "";
    /// Maschera di bit dei manometri/indicatori disattivati dall'utente (0 = tutti visibili)
    uint32_t    disabled_metrics_mask = 0;
    /// Abilitazione della scheda personalizzata [⭐ Custom] sulla dashboard principale
    bool        custom_tab_enabled    = true;
    /// Maschera di bit dei parametri telemetrici selezionati nel pannello personalizzato
    uint32_t    custom_tab_metrics_mask = 0;

    // --- Warmup e Stima Virtuale Olio Motore ---
    /// Abilitazione della logica di warmup e protezione motore
    bool        oil_warmup_enabled        = true;
    /// Temperatura refrigerante (°C) per l'avvio del countdown riscaldamento olio (default 80°C)
    float       oil_warmup_water_temp     = 80.0f;
    /// Durata countdown riscaldamento olio in secondi (default 300 s = 5 min)
    uint16_t    oil_warmup_countdown_sec  = 300;
    /// Soglia limite giri a freddo per avviso di protezione (default 3000 RPM)
    float       oil_cold_rpm_limit        = 3000.0f;
    /// Calcola e mostra la temperatura olio stimata sul cruscotto e temperature
    bool        oil_virtual_temp_display  = true;
};

/// Inizializza lo store NVS e legge i valori esistenti. Chiamare in main()
/// prima del display::init() per poter applicare subito la luminosita'.
bool init();

/// Restituisce lo snapshot corrente delle impostazioni.
/// Copia delle impostazioni correnti (thread-safe).
Settings current();

/// Aggiorna la luminosita' e la persiste. Applica subito al backlight.
void setBrightness(uint8_t percent);

/// Applica la luminosita' al pannello senza salvarla in NVS (trascinamento slider).
void previewBrightness(uint8_t percent);

/// Aggiorna l'unita' di misura temperatura.
void setTempUnit(TempUnit u);

/// Aggiorna l'unita' di misura velocita'.
void setSpeedUnit(SpeedUnit u);

/// Aggiorna l'unita' di misura pressione.
void setPressureUnit(PressureUnit u);

/// Converte un valore kPa nell'unita' di pressione corrente (kPa o bar).
/// Per PressureUnit::Both ritorna il valore in kPa (l'unita' mostrera' entrambe).
float pressureInCurrentUnit(float kpa);
/// Stringa dell'unita' di pressione attuale.
const char* pressureUnitSymbol();

/// Aggiorna lo stile dell'interfaccia (Classic o Motorsport).
void setUiTheme(UiTheme t);

/// Imposta lo stile dell'interfaccia (applicato al prossimo avvio).
void setUiStyle(UiStyle style);

/// Flag di sicurezza della nuova UI: impostato prima di costruirla, azzerato
/// dopo 10 s senza crash. Se all'avvio e' ancora impostato si torna al Classico.
bool isUiV2Pending();
void setUiV2Pending(bool pending);

/// Aggiorna la texture di sfondo dello schermo.
void setBgTexture(BgTexture texture);

/// Aggiorna l'orientamento dello schermo (applicato al prossimo reboot).
void setScreenOrientation(ScreenOrientation o);

/// Converte l'orientamento in gradi di rotazione LVGL (0/90/270).
uint16_t orientationToDegrees(ScreenOrientation o);

/// Imposta la connessione automatica OBD
void setAutoConnect(bool enable);

/// Modalita' di collegamento alla vettura
void setConnectionMode(ConnectionMode mode);
ConnectionMode getConnectionMode();

/// Configurazione CAN-Bus Diretto (Dual CAN TWAI)
void setCan1Enabled(bool en);
bool isCan1Enabled();
void setCan1Config(uint8_t tx, uint8_t rx, uint32_t baud);

void setCan2Enabled(bool en);
bool isCan2Enabled();
void setCan2Config(uint8_t tx, uint8_t rx, uint32_t baud);

void setCanListenOnly(bool listen_only);

// Compatibilita' legacy
void setCanPins(uint8_t tx, uint8_t rx);
void setCanBaudRate(uint32_t baud);

/// Imposta la modalita' simulazione / test telemetria
void setDemoMode(bool enable);

/// Imposta la modalita' debug avanzata (sblocca simulazione e mostra info splash)
void setDebugMode(bool enable);

/// Salva il MAC address del dongle OBD BLE
void setSavedDongleMac(const std::string& mac);

/// Salva le credenziali WiFi
void setSavedWifi(const std::string& ssid, const std::string& pass);

/// Converti un valore Celsius nell'unita' attuale.
float tempInCurrentUnit(float c);
/// Stringa dell'unita' temperatura attuale (es. "\u00b0C" o "\u00b0F").
const char* tempUnitSymbol();

/// Converti un valore km/h nell'unita' attuale.
float speedInCurrentUnit(float kmh);
/// Stringa dell'unita' velocita' attuale.
const char* speedUnitSymbol();

/// Ritorna true se l'indicatore telemetrico e' attivo e visibile in UI.
bool isMetricVisible(abarth::data::Metric m);
/// Attiva o disattiva la visibilita' di un indicatore telemetrico.
void setMetricVisible(abarth::data::Metric m, bool visible);

/// Gestione Scheda Personalizzata (Custom Tab)
bool isCustomTabEnabled();
void setCustomTabEnabled(bool enable);
bool isMetricInCustomTab(abarth::data::Metric m);
void setMetricInCustomTab(abarth::data::Metric m, bool in_custom);
uint32_t getCustomTabMask();
void setCustomTabMask(uint32_t mask);

/// Impostazioni Warmup e Protezione Motore
void setOilWarmupEnabled(bool enable);
void setOilWarmupWaterTemp(float temp_c);
void setOilWarmupCountdownSec(uint16_t sec);
void setOilColdRpmLimit(float rpm);
void setOilVirtualTempDisplay(bool enable);

}  // namespace abarth::settings
