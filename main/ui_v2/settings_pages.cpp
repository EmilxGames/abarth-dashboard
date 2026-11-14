// Impostazioni della UI Moderno: elenco categorie -> sottopagine con righe.
// Solo presentazione: ogni azione chiama le stesse funzioni della UI Classica,
// con le stesse protezioni (worker per le operazioni lente, guardie DTC, WiFi...).
#include "ui_v2/settings_pages.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <vector>
#include <esp_chip_info.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_idf_version.h>
#include <esp_system.h>
#include "lvgl.h"

#include "can/can_manager.h"
#include "config/alert_manager.h"
#include "config/connection.h"
#include "config/saved_links.h"
#include "config/settings.h"
#include "config/warmup_manager.h"
#include "display/lvgl_v8_port.h"
#include "obd/ble_stream.h"
#include "obd/obd_manager.h"
#include "storage/ota_updater.h"
#include "storage/rtc_manager.h"
#include "storage/sd_card_manager.h"
#include "storage/telemetry_logger.h"
#include "storage/web_server.h"
#include "ui_v2/async_ui.h"
#include "ui_v2/metric_info.h"
#include "ui_v2/sp_kit.h"
#include "ui_v2/ui_v2.h"
#include "ui_v2/theme.h"
#include "ui_v2/widgets.h"
#include "wifi_manager.h"

namespace abarth::ui_v2::settings_pages {

namespace {
using namespace abarth::settings;
using std::string;
using namespace theme;
lv_obj_t* g_root = nullptr;

string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
string fmt(const char* f, ...) {
    char b[256];
    va_list a;
    va_start(a, f);
    vsnprintf(b, sizeof(b), f, a);
    va_end(a);
    return b;
}
const char* yesno(bool v) { return v ? "S\xC3\xAC" : "No"; }

// =========================================================== COLLEGAMENTO
const char* mode_name(ConnectionMode m) {
    return m == ConnectionMode::BleObd ? "OBD Bluetooth" : m == ConnectionMode::WifiObd ? "OBD Wi-Fi" : "CAN diretto";
}

// Collegamenti salvati come su un telefono (config/saved_links): cercare e'
// sempre possibile, aggiungere non toglie nulla, cambiare modalita' non
// cancella niente.
using links::WifiRole;

void page_collegamento();
void page_web();
bool g_web_after_connect = false;   ///< avvia il web server appena la rete e' collegata
bool g_ble_scanning = false;
bool g_wifi_scanning = false;

void reopen_collegamento() { sp::popAll(); page_collegamento(); }

void start_web_server() {
    const esp_err_t err = web::startServer();
    if (err == ESP_OK) sp::message("Web Server", "Web server attivo.\nApri dal telefono: " + web::getServerUrl());
    else sp::message("Web Server", fmt("Avvio non riuscito (%s).", esp_err_to_name(err)));
}

/// Collega la rete; se va a buon fine la salva (o la aggiorna) nell'elenco
void wifi_connect(const string& ssid, const string& pw, WifiRole role) {
    sp::message("Wi-Fi", "Connessione a " + ssid + " in corso...");
    wifi::WifiManager::instance().connect(ssid, pw, [ssid, pw, role](bool ok, const std::string& ip, const std::string& gw) {
        async::postToUi([ok, ssid, pw, role, ip, gw]() {
            if (!ok) {
                g_web_after_connect = false;
                const string why = wifi::WifiManager::instance().lastError();
                sp::message("Wi-Fi", why.empty() ? "Connessione non riuscita. Controlla la password e che la rete sia vicina, poi riprova."
                                                 : "Wi-Fi non disponibile: " + why);
                return;
            }
            if (role == WifiRole::Dongle) {
                setSavedWifi(ssid, pw);   // rete del dongle in uso (finisce anche nell'elenco)
                if (current().connection_mode == ConnectionMode::WifiObd) obd::obd().connectWiFi(gw, 35000);
            } else {
                links::addWifi(ssid, pw, WifiRole::Internet);
            }
            if (g_web_after_connect) {
                g_web_after_connect = false;
                start_web_server();
                return;
            }
            sp::message("Wi-Fi", "Collegato a " + ssid + "\nIP: " + ip);
        });
    });
}

/// Collega una rete scegliendo l'uso: se servirebbe staccare il dongle Wi-Fi in uso, chiede conferma
void wifi_connect_checked(const string& ssid, const string& pw, WifiRole role) {
    auto& wm = wifi::WifiManager::instance();
    const bool dongle_link = current().connection_mode == ConnectionMode::WifiObd && wm.isConnected() &&
                             wm.getCurrentSSID() == current().saved_wifi_ssid && ssid != wm.getCurrentSSID();
    if (dongle_link) {
        sp::confirm("Wi-Fi", "Ora il Wi-Fi e' collegato al dongle OBD: collegandoti a " + ssid +
                    " i dati OBD si fermano finche' non torni alla rete del dongle.", "Collega",
                    [ssid, pw, role]() { wifi_connect(ssid, pw, role); }, false);
        return;
    }
    wifi_connect(ssid, pw, role);
}

/// Dopo la password: a cosa serve la rete (salvata con l'uso scelto)
void ask_role_then_connect(const string& ssid, const string& pw) {
    sp::choice("Uso della rete", {"Internet e web server", "Dongle OBD Wi-Fi"}, []() { return -1; },
               [ssid, pw](int i) {
                   const WifiRole role = i == 1 ? WifiRole::Dongle : WifiRole::Internet;
                   async::postToUi([ssid, pw, role]() { wifi_connect_checked(ssid, pw, role); });
               });
}

void open_wifi_results(const std::vector<wifi::WifiNetwork>& nets) {
    lv_obj_t* l = sp::push("Reti Wi-Fi");
    const string err = wifi::WifiManager::instance().lastError();
    sp::section(l, nets.empty() ? "NESSUNA RETE TROVATA" : "RETI TROVATE");
    if (nets.empty() && !err.empty()) sp::rowInfo(l, [err]() { return "Wi-Fi non disponibile: " + err; });
    for (const auto& n : nets) {
        const string ssid = n.ssid;
        const bool open = n.auth_mode == WIFI_AUTH_OPEN;
        links::WifiEntry saved;
        const bool known = links::findWifi(ssid, saved);
        sp::rowNav(l, ssid.c_str(), [n, known]() {
            // breve, cosi' anche i nomi di rete lunghi restano leggibili
            return known ? string("salvata") : fmt("%d dBm%s", n.rssi, n.auth_mode == WIFI_AUTH_OPEN ? " \xC2\xB7 aperta" : "");
        }, [ssid, open, known, saved]() {
            if (known) { wifi_connect_checked(ssid, saved.pass, saved.role); return; }   // password gia' salvata
            if (open) { ask_role_then_connect(ssid, ""); return; }
            sp::textInput(("Password " + ssid).c_str(), "", true, [ssid](const string& pw) {
                async::postToUi([ssid, pw]() { ask_role_then_connect(ssid, pw); });
            });
        });
    }
}

void wifi_scan() {
    // sempre permesso: la ricerca non stacca la rete collegata
    if (g_wifi_scanning) return;
    g_wifi_scanning = true;
    sp::message("Wi-Fi", "Ricerca reti in corso...");
    wifi::WifiManager::instance().startScan([](const std::vector<wifi::WifiNetwork>& nets) {
        async::postToUi([nets]() {
            g_wifi_scanning = false;
            const bool from_web = g_web_after_connect;
            sp::popAll();
            if (from_web) page_web(); else page_collegamento();
            open_wifi_results(nets);
        });
    });
}

void page_wifi_item(const string& ssid) {
    links::WifiEntry e;
    if (!links::findWifi(ssid, e)) return;
    lv_obj_t* l = sp::push(ssid.c_str());
    sp::section(l, "RETE SALVATA");
    sp::rowNav(l, "Uso", [ssid]() { links::WifiEntry x; return string(links::findWifi(ssid, x) ? links::roleName(x.role) : "-"); }, [ssid]() {
        links::WifiEntry x;
        if (!links::findWifi(ssid, x)) return;
        sp::choice("Uso della rete", {"Internet e web server", "Dongle OBD Wi-Fi"},
                   [ssid]() { links::WifiEntry y; return links::findWifi(ssid, y) ? static_cast<int>(y.role) : 0; },
                   [ssid](int i) { links::setWifiRole(ssid, i == 1 ? WifiRole::Dongle : WifiRole::Internet); });
    });
    sp::rowAction(l, "Collega", [ssid]() {
        links::WifiEntry x;
        if (links::findWifi(ssid, x)) wifi_connect_checked(ssid, x.pass, x.role);
    });
    sp::rowAction(l, "Rimuovi dall'elenco", [ssid]() {
        sp::confirm("Rimuovi rete", "Togliere " + ssid + " dalle reti salvate? Per ricollegarla servira' di nuovo la password.", "Rimuovi",
                    [ssid]() { links::removeWifi(ssid); async::postToUi([]() { reopen_collegamento(); }); }, true);
    }, true);
}

void open_ble_results(const std::vector<std::string>& devs) {
    lv_obj_t* l = sp::push("Dispositivi Bluetooth");
    sp::section(l, devs.empty() ? "NESSUN DISPOSITIVO TROVATO" : "DISPOSITIVI TROVATI");
    for (const auto& d : devs) {
        string mac, name = d;
        const size_t p1 = d.find('['), p2 = d.find(']');
        if (p1 != string::npos && p2 != string::npos) {
            mac = d.substr(p1 + 1, p2 - p1 - 1);
            name = d.substr(0, p1);
            while (!name.empty() && name.back() == ' ') name.pop_back();
        }
        sp::rowAction(l, d.c_str(), [mac, name]() {
            if (mac.empty()) return;
            links::addBle(name, mac);   // si aggiunge, non sostituisce
            setAutoConnect(true);
            obd::obd().connectBLE(mac);
            sp::popAll();
            page_collegamento();
            sp::message("Bluetooth", "Collegamento a " + (name.empty() ? mac : name) + " avviato.");
        });
    }
}

void ble_scan() {
    if (g_ble_scanning) return;
    g_ble_scanning = true;
    sp::message("Bluetooth", "Ricerca in corso (5 secondi)...");
    async::runInWorker([]() {
        obd::BleStream temp;
        temp.begin({});
        auto devs = temp.scanDevices(5);
        async::postToUi([devs]() { g_ble_scanning = false; reopen_collegamento(); open_ble_results(devs); });
    });
}

void page_ble_item(const string& mac) {
    string name;
    for (const auto& b : links::bleList()) if (b.mac == mac) name = b.name;
    lv_obj_t* l = sp::push(name.empty() ? mac.c_str() : name.c_str());
    sp::section(l, "DONGLE SALVATO");
    sp::rowValue(l, "MAC", [mac]() { return mac; });
    sp::rowAction(l, "Usa questo dongle", [mac]() {
        setAutoConnect(true);
        if (current().connection_mode == ConnectionMode::BleObd) {
            obd::obd().connectBLE(mac);
            reopen_collegamento();
            sp::message("Bluetooth", "Collegamento avviato.");
        } else {
            setSavedDongleMac(mac);
            reopen_collegamento();
            sp::message("Bluetooth", "Scelto: verra' usato quando la modalita' e' \"OBD Bluetooth\".");
        }
    });
    sp::rowAction(l, "Rimuovi dall'elenco", [mac]() {
        sp::confirm("Rimuovi dongle", "Togliere questo dongle dall'elenco? Potrai ritrovarlo con \"Cerca dispositivi\".", "Rimuovi", [mac]() {
            if (current().saved_dongle_mac == mac) obd::obd().forgetDevice();
            links::removeBle(mac);
            async::postToUi([]() { reopen_collegamento(); });
        }, true);
    }, true);
}

void page_collegamento() {
    lv_obj_t* l = sp::push("Collegamento");
    sp::section(l, "MODALIT\xC3\x80");
    sp::rowNav(l, "Modalit\xC3\xA0", []() { return string(mode_name(current().connection_mode)); }, []() {
        sp::choice("Modalit\xC3\xA0", {"OBD Bluetooth", "OBD Wi-Fi", "CAN diretto"},
                   []() { return static_cast<int>(current().connection_mode); },
                   [](int i) {
                       const auto m = static_cast<ConnectionMode>(i);
                       connection::switchMode(m);   // non cancella nulla di salvato
                       links::WifiEntry d;
                       if (m == ConnectionMode::WifiObd && current().saved_wifi_ssid.empty() && !links::lastWifi(WifiRole::Dongle, d))
                           sp::message("OBD Wi-Fi", "Collega la rete Wi-Fi del dongle: Collegamento > Cerca reti, poi scegli \"Dongle OBD Wi-Fi\".");
                   });
    });
    sp::rowToggle(l, "Connessione automatica", []() { return current().auto_connect; }, [](bool v) { setAutoConnect(v); });
    sp::rowValue(l, "Stato", []() {
        if (current().connection_mode == ConnectionMode::DirectCan) return string(can::canStateName(can::can().state(0)));
        return string(obd::stateName(obd::obd().state()));
    });
    sp::rowValue(l, "Ultimo errore", []() {
        const string e = current().connection_mode == ConnectionMode::DirectCan ? can::can().lastError(0) : obd::obd().lastError();
        return e.empty() ? string("nessuno") : e;
    });

    sp::section(l, "DONGLE BLUETOOTH SALVATI");
    const auto bl = links::bleList();
    if (bl.empty()) sp::rowInfo(l, []() { return string("Nessun dongle salvato: usa \"Cerca dispositivi\"."); });
    for (const auto& b : bl) {
        const string mac = b.mac;
        sp::rowNav(l, b.name.empty() ? b.mac.c_str() : b.name.c_str(),
                   [mac]() { return string(current().saved_dongle_mac == mac ? "in uso" : mac.c_str()); },
                   [mac]() { page_ble_item(mac); });
    }
    sp::rowAction(l, "Cerca dispositivi", []() { ble_scan(); });

    sp::section(l, "RETI WI-FI SALVATE");
    const auto wl = links::wifiList();
    if (wl.empty()) sp::rowInfo(l, []() { return string("Nessuna rete salvata: usa \"Cerca reti\"."); });
    for (const auto& w : wl) {
        const string ssid = w.ssid;
        const WifiRole role = w.role;
        sp::rowNav(l, ssid.c_str(), [ssid, role]() {
            auto& wm = wifi::WifiManager::instance();
            const bool on = wm.isConnected() && wm.getCurrentSSID() == ssid;
            return string(role == WifiRole::Dongle ? "Dongle OBD" : "Internet") + (on ? " \xC2\xB7 collegata" : "");
        }, [ssid]() { page_wifi_item(ssid); });
    }
    sp::rowAction(l, "Cerca reti", []() { g_web_after_connect = false; wifi_scan(); });
    sp::rowValue(l, "Collegato a", []() {
        auto& wm = wifi::WifiManager::instance();
        return wm.isConnected() ? wm.getCurrentSSID() + " \xC2\xB7 " + wm.getCurrentIP() : string("nessuna rete");
    });
    sp::rowAction(l, "Scollega Wi-Fi", []() {
        wifi::WifiManager::instance().disconnect();   // le reti restano salvate
        sp::message("Wi-Fi", "Scollegato. Le reti restano salvate.");
    });
}

// =========================================================== CAN
string can_diag(uint8_t b) {
    auto& c = can::can();
    const string e = c.lastError(b);
    return fmt("Stato: %s\nFrame ricevuti: %lu  (%.0f/s)\nInviati: %lu   Errori: %lu\nUltimo errore: %s",
               can::canStateName(c.state(b)), (unsigned long)c.rxFrames(b), c.rxRateFps(b),
               (unsigned long)c.txFrames(b), (unsigned long)c.busErrors(b), e.empty() ? "nessuno" : e.c_str());
}

// =========================================================== SCHEMA COLLEGAMENTI
// Pagina statica: niente si aggiorna, si disegna una volta all'apertura.
struct Wire { const char* from; bool to_module; const char* to; };

constexpr int32_t kWireRowH = 44;
constexpr int32_t kColFrom = kCardPad;         ///< lato sinistro
constexpr int32_t kColArrow = 254;             ///< freccia (direzione del segnale)
constexpr int32_t kColTo = 306;                ///< lato destro

/// Blocco a due colonne: intestazioni, righe "da → a" e una nota opzionale
void wire_block(lv_obj_t* list, const char* head_l, const char* head_r, const Wire* rows, int n, const char* note) {
    int32_t note_h = 0;
    if (note) {
        lv_point_t sz;
        lv_text_get_size(&sz, note, fontLabel(), 0, 4, sp::rowWidth() - 2 * kCardPad, LV_TEXT_FLAG_NONE);
        note_h = sz.y + 12;
    }
    lv_obj_t* r = sp::rowCustom(list, 58 + n * kWireRowH + note_h + 18);
    if (!r) return;
    const int32_t lh = lv_font_get_line_height(fontUnit());
    lv_obj_t* h = w::label(r, head_l, fontLabel(), kDim, true);
    lv_obj_set_pos(h, kColFrom, 22);
    h = w::label(r, head_r, fontLabel(), kDim, true);
    lv_obj_set_pos(h, kColTo, 22);
    for (int i = 0; i < n; ++i) {
        const int32_t y = 58 + i * kWireRowH + (kWireRowH - lh) / 2;
        lv_obj_t* a = w::label(r, rows[i].from, fontUnit(), kText);
        lv_obj_set_pos(a, kColFrom, y);
        if (rows[i].to[0]) {
            lv_obj_t* ar = w::label(r, rows[i].to_module ? "\xE2\x86\x92" : "\xE2\x86\x90", fontUnit(), accent());
            lv_obj_set_pos(ar, kColArrow, y);
        }
        lv_obj_t* b = w::label(r, rows[i].to[0] ? rows[i].to : "non usato", fontUnit(), rows[i].to[0] ? kText : kDim);
        lv_obj_set_pos(b, kColTo, y);
    }
    if (note) {
        lv_obj_t* nl = w::label(r, note, fontLabel(), kSecondary);
        lv_label_set_long_mode(nl, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_line_space(nl, 4, 0);
        lv_obj_set_width(nl, sp::rowWidth() - 2 * kCardPad);
        lv_obj_set_pos(nl, kColFrom, 58 + n * kWireRowH + 10);
    }
}

/// Presa OBD-II vista di fronte (in auto): 1-8 sopra, 9-16 sotto; i pin usati nel colore accento
void obd_socket(lv_obj_t* list) {
    constexpr int32_t kH = 232;
    lv_obj_t* r = sp::rowCustom(list, kH);
    if (!r) return;
    const int32_t W = sp::rowWidth();
    // contorno trapezoidale (piu' largo in alto, come la presa vera)
    static lv_point_precise_t pts[5];
    const int32_t x0 = 40, x1 = W - 40, y0 = 26, y1 = 160, inset = 26;
    pts[0] = {x0, y0}; pts[1] = {x1, y0}; pts[2] = {x1 - inset, y1}; pts[3] = {x0 + inset, y1}; pts[4] = {x0, y0};
    lv_obj_t* ln = lv_line_create(r);
    lv_line_set_points(ln, pts, 5);
    lv_obj_set_style_line_width(ln, 2, 0);
    lv_obj_set_style_line_color(ln, c(kTrack), 0);
    lv_obj_set_style_line_rounded(ln, true, 0);
    constexpr int32_t kPin = 40, kPinGap = 11;
    const int32_t px0 = (W - (8 * kPin + 7 * kPinGap)) / 2;
    for (int i = 0; i < 16; ++i) {
        const int n = i + 1;
        const bool used = n == 1 || n == 4 || n == 5 || n == 6 || n == 9 || n == 14;
        const int32_t x = px0 + (i % 8) * (kPin + kPinGap);
        const int32_t y = i < 8 ? y0 + 16 : y0 + 16 + kPin + 22;
        lv_obj_t* p = w::box(r, x, y, kPin, kPin, used ? accent() : kCard, 10);
        if (!used) {
            lv_obj_set_style_border_width(p, 2, 0);
            lv_obj_set_style_border_color(p, c(kTrack), 0);
        }
        char num[4];
        std::snprintf(num, sizeof(num), "%d", n);
        lv_obj_t* nl = w::label(p, num, fontLabel(), used ? kText : kDim);
        lv_obj_center(nl);
    }
    // legenda su tre colonne fisse
    static const char* const kLegend[3][2] = {{"6 \xC2\xB7 14", "C-CAN"}, {"1 \xC2\xB7 9", "B-CAN"}, {"4 \xC2\xB7 5", "massa"}};
    for (int i = 0; i < 3; ++i) {
        const int32_t lx = kCardPad + i * ((W - 2 * kCardPad) / 3);
        lv_obj_t* a = w::label(r, kLegend[i][0], fontLabel(), kText);
        lv_obj_set_pos(a, lx, y1 + 24);
        lv_obj_t* b = w::label(r, kLegend[i][1], fontLabel(), kSecondary);
        lv_obj_set_pos(b, lx + w::textWidth(kLegend[i][0], fontLabel()) + 10, y1 + 24);
    }
}

void page_schema() {
    lv_obj_t* l = sp::push("Schema collegamenti");
    const auto s = current();
    char tx1[16], rx1[16], tx2[16], rx2[16], h1[48], h2[64];
    std::snprintf(tx1, sizeof(tx1), "GPIO %u", s.can1_tx_pin);
    std::snprintf(rx1, sizeof(rx1), "GPIO %u", s.can1_rx_pin);
    std::snprintf(tx2, sizeof(tx2), "GPIO %u", s.can2_tx_pin);
    std::snprintf(rx2, sizeof(rx2), "GPIO %u", s.can2_rx_pin);
    std::snprintf(h1, sizeof(h1), "CAN MOTORE (C-CAN) \xC2\xB7 %lu KBIT/S", (unsigned long)(s.can1_baud_rate / 1000));
    std::snprintf(h2, sizeof(h2), "CAN CARROZZERIA (B-CAN) \xC2\xB7 %lu KBIT/S", (unsigned long)(s.can2_baud_rate / 1000));

    sp::section(l, "PRESA OBD-II \xC2\xB7 VISTA DI FRONTE");
    obd_socket(l);

    // Modulo 1: WeAct ISOCANFD (CA-IS2062A, isolato)
    sp::section(l, h1);
    const Wire d1[] = {{tx1, true, "TXD"}, {rx1, false, "RXD"}, {"5 V", true, "VDDP (5V)"},
                       {"3,3 V", true, "VDDL (3V3)"}, {"GND", true, "GND"}};
    wire_block(l, "DISPLAY", "MODULO 1", d1, 5, "Servono sia 5 V sia 3,3 V: senza 5 V il lato CAN del modulo resta spento.");
    const Wire o1[] = {{"CAN-H", true, "pin 6"}, {"CAN-L", true, "pin 14"}, {"GND", true, "pin 4 / 5"}};
    wire_block(l, "MODULO 1", "PRESA OBD", o1, 3, "Jumper 120 \xCE\xA9 sul modulo: lasciare aperto (la terminazione c'\xC3\xA8 gi\xC3\xA0 in auto).");

    // Modulo 2: stessa struttura, rete carrozzeria (solo ascolto)
    sp::section(l, h2);
    const Wire d2[] = {{tx2, true, "TXD"}, {rx2, false, "RXD"}, {"5 V", true, "VDDP (5V)"},
                       {"3,3 V", true, "VDDL (3V3)"}, {"GND", true, "GND"}};
    wire_block(l, "DISPLAY", "MODULO 2", d2, 5, "Rete carrozzeria: il display la legge soltanto.");
    const Wire o2[] = {{"CAN-H", true, "pin 1"}, {"CAN-L", true, "pin 9"}, {"GND", true, "pin 4 / 5"}};
    wire_block(l, "MODULO 2", "PRESA OBD", o2, 3, "Jumper 120 \xCE\xA9 sul modulo: lasciare aperto.");

    sp::section(l, "ALIMENTAZIONE");
    const Wire pw[] = {{"Presa USB auto", true, "USB-C display"}, {"Pin 16 \xC2\xB7 +12 V", true, ""}};
    wire_block(l, "DA", "A", pw, 2, "5 V dalla presa USB dell'auto: si spegne 1\xE2\x80\x93" "2 minuti dopo aver spento il motore.");
}

void page_can() {
    lv_obj_t* l = sp::push("CAN diretto");
    sp::section(l, "C-CAN MOTORE (CAN 1)");
    sp::rowToggle(l, "Abilitato", []() { return current().can1_enabled; }, [](bool en) {
        setCan1Enabled(en);
        if (current().connection_mode == ConnectionMode::DirectCan) { if (en) can::can().startBus(0); else can::can().stopBus(0); }
    });
    sp::rowNav(l, "Velocit\xC3\xA0 bus", []() { return fmt("%lu kbit/s", (unsigned long)(current().can1_baud_rate / 1000)); }, []() {
        static const uint32_t b[] = {125000, 250000, 500000, 1000000};
        sp::choice("Velocit\xC3\xA0 C-CAN", {"125 kbit/s", "250 kbit/s", "500 kbit/s", "1000 kbit/s"},
                   []() { for (int i = 0; i < 4; ++i) if (b[i] == current().can1_baud_rate) return i; return -1; },
                   [](int i) { const auto s = current(); setCan1Config(s.can1_tx_pin, s.can1_rx_pin, b[i]); can::can().restartBus(0); });
    });
    sp::rowAction(l, "Riavvia C-CAN", []() { can::can().restartBus(0); });
    sp::rowInfo(l, []() { return can_diag(0); });

    sp::section(l, "B-CAN CARROZZERIA (CAN 2)");
    sp::rowToggle(l, "Abilitato", []() { return current().can2_enabled; }, [](bool en) {
        setCan2Enabled(en);
        if (current().connection_mode == ConnectionMode::DirectCan) { if (en) can::can().startBus(1); else can::can().stopBus(1); }
    });
    sp::rowNav(l, "Velocit\xC3\xA0 bus", []() { return fmt("%lu kbit/s", (unsigned long)(current().can2_baud_rate / 1000)); }, []() {
        static const uint32_t b[] = {50000, 100000, 125000, 250000, 500000};
        sp::choice("Velocit\xC3\xA0 B-CAN", {"50 kbit/s", "100 kbit/s", "125 kbit/s", "250 kbit/s", "500 kbit/s"},
                   []() { for (int i = 0; i < 5; ++i) if (b[i] == current().can2_baud_rate) return i; return -1; },
                   [](int i) { const auto s = current(); setCan2Config(s.can2_tx_pin, s.can2_rx_pin, b[i]); can::can().restartBus(1); });
    });
    sp::rowAction(l, "Riavvia B-CAN", []() { can::can().restartBus(1); });
    sp::rowInfo(l, []() { return can_diag(1); });

    sp::section(l, "SICUREZZA");
    sp::rowToggle(l, "Solo ascolto (listen-only)", []() { return current().can_listen_only; }, [](bool lo) {
        setCanListenOnly(lo);
        if (current().connection_mode == ConnectionMode::DirectCan) can::can().restart();
    });
    sp::rowInfo(l, []() {
        return string("Con 'solo ascolto' disattivato il display invia richieste OBD (0x7E0) solo dopo aver ricevuto "
                      "traffico valido, le sospende a quadro spento e torna in ascolto dopo 3 bus-off.");
    });
    sp::section(l, "COLLEGAMENTI");
    sp::rowNav(l, "Vedi schema collegamenti", nullptr, []() { page_schema(); });
}

// =========================================================== ORDINE PAGINE
const char* const kDataPageNames[page_order::kData] = {"GUIDA", "TEMPERATURE", "PRESSIONI", "ELETTRICO", "PERSONALE"};

void page_order_edit();
lv_obj_t* g_order_btns[page_order::kData][3] = {};   ///< solo per i test: occhio, su, giu'

/// Pulsantino con simbolo (frecce, occhio) dentro una riga dell'ordine pagine
lv_obj_t* order_btn(lv_obj_t* row, int32_t x, const char* sym, bool enabled, lv_event_cb_t cb, int arg) {
    lv_obj_t* b = w::box(row, x, (kRowH - 52) / 2, 60, 52, kTrack, 14);
    lv_obj_t* l = w::label(b, sym, &lv_font_montserrat_22, enabled ? kText : kDim);
    lv_obj_center(l);
    if (!enabled) {
        lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
        return b;
    }
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_bg_color(b, c(kCardPress), LV_STATE_PRESSED);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(arg)));
    return b;
}

void order_apply_and_reopen(const page_order::Order& o) {
    setPageOrder(o);
    sp::pop();             // ridisegna la sottopagina con il nuovo ordine
    page_order_edit();
}

int order_arg(lv_event_t* e) { return static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e))); }

void page_order_edit() {
    lv_obj_t* l = sp::push("Ordine pagine");
    const page_order::Order o = pageOrder();
    sp::section(l, "DALLA PRIMA ALL'ULTIMA");
    const int32_t W = sp::rowWidth();
    for (int pos = 0; pos < page_order::kData; ++pos) {
        const int pg = o.order[pos];
        const bool hidden = page_order::isHidden(o, pg);
        lv_obj_t* r = sp::rowCustom(l, kRowH);
        if (!r) return;
        char num[4];
        std::snprintf(num, sizeof(num), "%d", pos + 1);
        lv_obj_t* n = w::label(r, num, fontBody(), kDim);
        lv_obj_set_pos(n, kCardPad, (kRowH - lv_font_get_line_height(fontBody())) / 2);
        lv_obj_t* name = w::label(r, kDataPageNames[pg], fontBody(), hidden ? kDim : kText);
        lv_obj_set_pos(name, kCardPad + 36, (kRowH - lv_font_get_line_height(fontBody())) / 2);
        // a destra: mostra/nascondi, su, giu'
        const int32_t xr = W - kCardPad + 10;
        g_order_btns[pos][2] = order_btn(r, xr - 60, LV_SYMBOL_DOWN, pos < page_order::kData - 1, [](lv_event_t* e) {
            page_order::Order no = pageOrder();
            if (page_order::moveDown(no, order_arg(e))) order_apply_and_reopen(no);
        }, pos);
        g_order_btns[pos][1] = order_btn(r, xr - 128, LV_SYMBOL_UP, pos > 0, [](lv_event_t* e) {
            page_order::Order no = pageOrder();
            if (page_order::moveUp(no, order_arg(e))) order_apply_and_reopen(no);
        }, pos);
        lv_obj_t* eye = order_btn(r, xr - 208, hidden ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN, true, [](lv_event_t* e) {
            page_order::Order no = pageOrder();
            const int page = order_arg(e);
            if (!page_order::setVisible(no, page, page_order::isHidden(no, page))) {
                sp::message("Ordine pagine", "Almeno una pagina deve restare visibile.");
                return;
            }
            order_apply_and_reopen(no);
        }, pg);
        g_order_btns[pos][0] = eye;
        if (hidden) lv_obj_set_style_text_color(lv_obj_get_child(eye, 0), c(kDim), 0);
    }
    sp::rowInfo(l, []() {
        return string("La prima pagina \xC3\xA8 quella che vedi all'accensione. L'occhio mostra o nasconde una pagina. "
                      "IMPOSTAZIONI resta sempre ultima (e c'\xC3\xA8 sempre la rotellina).");
    });
    sp::section(l, "");
    sp::rowAction(l, "Ripristina ordine predefinito", []() { order_apply_and_reopen(page_order::defaults()); });
}

string order_summary() {
    const page_order::Order& o = pageOrder();
    uint8_t v[page_order::kData];
    const int n = page_order::visible(o, v);
    if (n == page_order::kData && std::memcmp(o.order, page_order::defaults().order, sizeof(o.order)) == 0) return "Predefinito";
    return fmt("Prima: %s", kDataPageNames[v[0]]);
}

// =========================================================== DISPLAY
void page_display() {
    lv_obj_t* l = sp::push("Display");
    sp::section(l, "SCHERMO");
    sp::rowSlider(l, "Luminosit\xC3\xA0", 10, 100, []() { return static_cast<int>(current().brightness); },
                  [](int v) { previewBrightness(static_cast<uint8_t>(v)); }, [](int v) { setBrightness(static_cast<uint8_t>(v)); });
    sp::rowNav(l, "Orientamento", []() {
        const auto o = current().screen_orientation;
        return string(o == ScreenOrientation::Landscape ? "Orizzontale" : o == ScreenOrientation::Portrait ? "Verticale" : "Verticale capovolto");
    }, []() {
        sp::choice("Orientamento", {"Orizzontale", "Verticale", "Verticale capovolto"},
                   []() { return static_cast<int>(current().screen_orientation); },
                   [](int i) {
                       if (i == static_cast<int>(current().screen_orientation)) return;
                       setScreenOrientation(static_cast<ScreenOrientation>(i));
                       sp::confirm("Orientamento", "Il nuovo orientamento si applica al riavvio.", "Riavvia ora", []() { esp_restart(); }, false);
                   });
    });
    sp::section(l, "INTERFACCIA");
    sp::rowNav(l, "Ordine pagine", []() { return order_summary(); }, []() { page_order_edit(); });
    sp::rowNav(l, "Stile interfaccia", []() { return string(current().ui_style == UiStyle::Racing ? "Moderno" : "Classica"); }, []() {
        sp::choice("Stile interfaccia", {"Classica", "Moderno"}, []() { return static_cast<int>(current().ui_style); }, [](int i) {
            if (i == static_cast<int>(current().ui_style)) return;
            setUiStyle(static_cast<UiStyle>(i));
            sp::confirm("Stile interfaccia", "Lo stile si applica al riavvio.", "Riavvia ora", []() { esp_restart(); }, false);
        });
    });
    sp::rowNav(l, "Colore accento", []() { return string(theme::kAccentNames[current().ui_accent & 3]); }, []() {
        sp::choice("Colore accento", {"Bianco", "Rosso Abarth", "Azzurro ghiaccio", "Ambra"},
                   []() { return static_cast<int>(current().ui_accent); },
                   [](int i) {
                       if (i == current().ui_accent) return;
                       setUiAccent(static_cast<uint8_t>(i));
                       sp::confirm("Colore accento", "Il colore si applica al riavvio.", "Riavvia ora", []() { esp_restart(); }, false);
                   });
    });
    sp::rowStepper(l, "Zona rossa giri", []() { return fmt("%u", current().redline_rpm); },
                   []() { setRedlineRpm(static_cast<uint16_t>(current().redline_rpm - 250)); },
                   []() { setRedlineRpm(static_cast<uint16_t>(current().redline_rpm + 250)); });
    sp::rowNav(l, "Sfondo (Classica)", []() {
        static const char* n[] = {"Originale", "Carbonio", "Scacchi", "Tricolore", "Scorpione", "Asfalto"};
        return string(n[static_cast<int>(current().bg_texture) % 6]);
    }, []() {
        sp::choice("Sfondo (Classica)", {"Originale", "Carbonio", "Scacchi", "Tricolore", "Scorpione", "Asfalto"},
                   []() { return static_cast<int>(current().bg_texture); },
                   [](int i) { setBgTexture(static_cast<BgTexture>(i)); });
    });
}

// =========================================================== UNITA'
void page_unita() {
    lv_obj_t* l = sp::push("Unit\xC3\xA0 di misura");
    sp::section(l, "UNIT\xC3\x80");
    sp::rowNav(l, "Temperatura", []() { return string(current().temp_unit == TempUnit::Celsius ? "\xC2\xB0" "C" : "\xC2\xB0" "F"); }, []() {
        sp::choice("Temperatura", {"Celsius (\xC2\xB0" "C)", "Fahrenheit (\xC2\xB0" "F)"}, []() { return static_cast<int>(current().temp_unit); },
                   [](int i) { setTempUnit(static_cast<TempUnit>(i)); });
    });
    sp::rowNav(l, "Velocit\xC3\xA0", []() { return string(current().speed_unit == SpeedUnit::KmH ? "km/h" : "mph"); }, []() {
        sp::choice("Velocit\xC3\xA0", {"Chilometri orari (km/h)", "Miglia orarie (mph)"}, []() { return static_cast<int>(current().speed_unit); },
                   [](int i) { setSpeedUnit(static_cast<SpeedUnit>(i)); });
    });
    sp::rowNav(l, "Pressione", []() {
        const auto p = current().pressure_unit;
        return string(p == PressureUnit::Kpa ? "kPa" : p == PressureUnit::Bar ? "bar" : "kPa + bar");
    }, []() {
        sp::choice("Pressione", {"kPa", "bar", "kPa + bar"}, []() { return static_cast<int>(current().pressure_unit); },
                   [](int i) { setPressureUnit(static_cast<PressureUnit>(i)); });
    });
}

// =========================================================== ALLARMI
const char* const kAutoClose[] = {"Solo OK", "3 s", "5 s", "10 s", "15 s"};
const uint8_t kAutoCloseSec[] = {0, 3, 5, 10, 15};
int auto_close_idx(uint8_t s) { for (int i = 0; i < 5; ++i) if (kAutoCloseSec[i] == s) return i; return 0; }

void page_rule(config::AlertType t) {
    auto* r = config::alerts().getRule(t);
    if (!r) return;
    lv_obj_t* l = sp::push(r->name.c_str());
    sp::section(l, "ALLARME");
    sp::rowToggle(l, "Attivo", [t]() { auto* x = config::alerts().getRule(t); return x && x->enabled; },
                  [t](bool v) { config::alerts().setRuleEnabled(t, v); });
    // passo e formato in base al tipo di allarme
    float step = 1.0f;
    const char* f1 = "%.0f";
    if (t == config::AlertType::HighBoostPressure) { step = 0.05f; f1 = "%.2f bar"; }
    else if (t == config::AlertType::LowBatteryVoltage) { step = 0.1f; f1 = "%.1f V"; }
    else if (t == config::AlertType::ColdEngineRpm) { step = 100.0f; f1 = "%.0f giri"; }
    else f1 = "%.0f \xC2\xB0" "C";
    const string fmt1 = f1;
    sp::rowStepper(l, t == config::AlertType::ColdEngineRpm ? "Giri massimi" : "Soglia",
                   [t, fmt1]() { auto* x = config::alerts().getRule(t); return x ? fmt(fmt1.c_str(), x->threshold1) : string("-"); },
                   [t, step]() { auto* x = config::alerts().getRule(t); if (x) config::alerts().setRuleThreshold1(t, x->threshold1 - step); },
                   [t, step]() { auto* x = config::alerts().getRule(t); if (x) config::alerts().setRuleThreshold1(t, x->threshold1 + step); });
    if (t == config::AlertType::ColdEngineRpm) {
        sp::rowStepper(l, "Acqua sotto",
                       [t]() { auto* x = config::alerts().getRule(t); return x ? fmt("%.0f \xC2\xB0" "C", x->threshold2) : string("-"); },
                       [t]() { auto* x = config::alerts().getRule(t); if (x) config::alerts().setRuleThreshold2(t, x->threshold2 - 1); },
                       [t]() { auto* x = config::alerts().getRule(t); if (x) config::alerts().setRuleThreshold2(t, x->threshold2 + 1); });
    }
    sp::rowNav(l, "Chiusura automatica", [t]() { auto* x = config::alerts().getRule(t); return string(kAutoClose[auto_close_idx(x ? x->auto_close_sec : 0)]); }, [t]() {
        sp::choice("Chiusura automatica", {kAutoClose, kAutoClose + 5},
                   [t]() { auto* x = config::alerts().getRule(t); return auto_close_idx(x ? x->auto_close_sec : 0); },
                   [t](int i) { config::alerts().setRuleAutoClose(t, kAutoCloseSec[i]); });
    });
    sp::rowAction(l, "Prova allarme", [t]() {
        auto* x = config::alerts().getRule(t);
        if (x) config::alerts().triggerAlertPopup(*x, x->threshold1 + (t == config::AlertType::ColdEngineRpm ? 300.0f : 5.0f), x->threshold2 - 10.0f, true);
    });
}

void page_custom_rule(uint8_t id);

string op_name(config::AlertOp o) { return o == config::AlertOp::GreaterThan ? "maggiore di" : o == config::AlertOp::LessThan ? "minore di" : "uguale a"; }

void page_custom_rule(uint8_t id) {
    auto* r = config::alerts().getCustomRule(id);
    if (!r) return;
    lv_obj_t* l = sp::push(r->name.c_str());
    auto rule = [id]() { return config::alerts().getCustomRule(id); };
    sp::section(l, "ALLARME PERSONALIZZATO");
    sp::rowToggle(l, "Attivo", [rule]() { auto* x = rule(); return x && x->enabled; },
                  [rule](bool v) { if (auto* x = rule()) { x->enabled = v; config::alerts().saveCustomRules(); } });
    sp::rowNav(l, "Nome", [rule]() { auto* x = rule(); return x ? x->name : string(); }, [rule]() {
        auto* x = rule();
        if (!x) return;
        sp::textInput("Nome allarme", x->name, false, [rule](const string& t) { if (auto* y = rule()) { y->name = t; config::alerts().saveCustomRules(); } });
    });
    sp::rowNav(l, "Grandezza", [rule]() { auto* x = rule(); const auto* mi = x ? metricInfo(x->metric) : nullptr; return string(mi ? mi->long_name : "-"); }, [rule]() {
        size_t n = 0;
        const MetricInfo* cat = metricCatalog(n);
        std::vector<string> names;
        for (size_t i = 0; i < n; ++i) names.push_back(cat[i].long_name);
        sp::choice("Grandezza", names,
                   [rule, cat, n]() { auto* x = rule(); for (size_t i = 0; i < n; ++i) if (x && cat[i].metric == x->metric) return static_cast<int>(i); return -1; },
                   [rule, cat](int i) { if (auto* x = rule()) { x->metric = cat[i].metric; config::alerts().saveCustomRules(); } });
    });
    sp::rowNav(l, "Condizione", [rule]() { auto* x = rule(); return x ? op_name(x->op) : string(); }, [rule]() {
        sp::choice("Condizione", {"maggiore di", "minore di", "uguale a"}, [rule]() { auto* x = rule(); return x ? static_cast<int>(x->op) : 0; },
                   [rule](int i) { if (auto* x = rule()) { x->op = static_cast<config::AlertOp>(i); config::alerts().saveCustomRules(); } });
    });
    auto step_of = [rule]() {
        auto* x = rule();
        if (!x) return 1.0f;
        const auto* mi = metricInfo(x->metric);
        if (x->metric == data::Metric::Rpm) return 100.0f;
        return (mi && mi->decimals >= 2) ? 0.05f : (mi && mi->decimals == 1) ? 0.1f : 1.0f;
    };
    sp::rowStepper(l, "Soglia", [rule]() { auto* x = rule(); return x ? fmt("%.2f", x->threshold) : string("-"); },
                   [rule, step_of]() { if (auto* x = rule()) { x->threshold -= step_of(); config::alerts().saveCustomRules(); } },
                   [rule, step_of]() { if (auto* x = rule()) { x->threshold += step_of(); config::alerts().saveCustomRules(); } });
    sp::rowNav(l, "Messaggio", [rule]() { auto* x = rule(); return x ? x->custom_text : string(); }, [rule]() {
        auto* x = rule();
        if (!x) return;
        sp::textInput("Messaggio", x->custom_text, false, [rule](const string& t) { if (auto* y = rule()) { y->custom_text = t; config::alerts().saveCustomRules(); } });
    });
    sp::rowNav(l, "Chiusura automatica", [rule]() { auto* x = rule(); return string(kAutoClose[auto_close_idx(x ? x->auto_close_sec : 0)]); }, [rule]() {
        sp::choice("Chiusura automatica", {kAutoClose, kAutoClose + 5}, [rule]() { auto* x = rule(); return auto_close_idx(x ? x->auto_close_sec : 0); },
                   [rule](int i) { if (auto* x = rule()) { x->auto_close_sec = kAutoCloseSec[i]; config::alerts().saveCustomRules(); } });
    });
    sp::rowAction(l, "Prova", [rule]() { if (auto* x = rule()) config::alerts().triggerCustomAlertPopup(*x, x->threshold + 10.0f, true); });
    sp::rowAction(l, "Elimina allarme", [id]() {
        sp::confirm("Elimina allarme", "L'allarme personalizzato verra' eliminato.", "Elimina", [id]() {
            config::alerts().removeCustomRule(id);
            sp::pop();
        }, true);
    }, true);
}

void page_allarmi() {
    lv_obj_t* l = sp::push("Allarmi");
    sp::section(l, "ALLARMI STANDARD");
    for (const auto& r : config::alerts().rules()) {
        const auto t = r.type;
        sp::rowNav(l, r.name.c_str(), [t]() { auto* x = config::alerts().getRule(t); return string(x && x->enabled ? "Attivo" : "Spento"); },
                   [t]() { page_rule(t); });
    }
    sp::section(l, "ALLARMI PERSONALIZZATI");
    for (const auto& r : config::alerts().customRules()) {
        const uint8_t id = r.id;
        sp::rowNav(l, r.name.c_str(), [id]() { auto* x = config::alerts().getCustomRule(id); return string(x && x->enabled ? "Attivo" : "Spento"); },
                   [id]() { page_custom_rule(id); });
    }
    sp::rowAction(l, "+ Nuovo allarme", []() {
        auto* r = config::alerts().addCustomRule();
        if (!r) { sp::message("Allarmi", "Numero massimo di allarmi personalizzati raggiunto."); return; }
        config::alerts().saveCustomRules();
        const uint8_t id = r->id;
        sp::pop();
        page_allarmi();
        page_custom_rule(id);
    });

    sp::section(l, "RISCALDAMENTO OLIO");
    sp::rowToggle(l, "Protezione motore freddo", []() { return current().oil_warmup_enabled; }, [](bool v) { setOilWarmupEnabled(v); });
    sp::rowStepper(l, "Acqua a regime", []() { return fmt("%.0f \xC2\xB0" "C", current().oil_warmup_water_temp); },
                   []() { setOilWarmupWaterTemp(current().oil_warmup_water_temp - 1); },
                   []() { setOilWarmupWaterTemp(current().oil_warmup_water_temp + 1); });
    sp::rowStepper(l, "Tempo olio", []() { const unsigned v = current().oil_warmup_countdown_sec; return fmt("%u:%02u min", v / 60, v % 60); },
                   []() { const auto v = current().oil_warmup_countdown_sec; setOilWarmupCountdownSec(v > 90 ? v - 30 : 60); },
                   []() { setOilWarmupCountdownSec(current().oil_warmup_countdown_sec + 30); });
    sp::rowStepper(l, "Giri max a freddo", []() { return fmt("%.0f", current().oil_cold_rpm_limit); },
                   []() { setOilColdRpmLimit(current().oil_cold_rpm_limit - 100); },
                   []() { setOilColdRpmLimit(current().oil_cold_rpm_limit + 100); });
    sp::rowToggle(l, "Olio virtuale in plancia", []() { return current().oil_virtual_temp_display; }, [](bool v) { setOilVirtualTempDisplay(v); });
    sp::rowAction(l, "Prova allarme freddo", []() { warmup::warmup_mgr().triggerColdAlert(3400.0f, 52.0f, true); });
    sp::rowAction(l, "Prova olio in temperatura", []() { warmup::warmup_mgr().triggerReadyAlert(true); });
    sp::rowAction(l, "Riavvia ciclo riscaldamento", []() { warmup::warmup_mgr().resetCycle(); });
}

// =========================================================== DIAGNOSTICA ECU
void page_dtc() {
    lv_obj_t* l = sp::push("Diagnostica ECU");
    sp::section(l, "CODICI ERRORE");
    sp::rowAction(l, "Leggi errori (DTC)", []() {
        if (!async::runInWorker([]() {
                std::vector<obd::DtcItem> codes;
                string msg;
                const bool ok = obd::obd().readDtcCodes(codes, msg);
                async::postToUi([codes, msg, ok]() {
                    lv_obj_t* r = sp::push("Errori ECU");
                    sp::section(r, ok ? "RISULTATO" : "ERRORE");
                    sp::rowInfo(r, [msg]() { return msg; });
                    for (const auto& c : codes) {
                        const string t = c.code + "\n" + c.description;
                        sp::rowInfo(r, [t]() { return t; });
                    }
                });
            })) return;
        sp::message("Diagnostica", "Lettura errori in corso...");
    });
    sp::rowAction(l, "Cancella errori e spia motore", []() {
        string why;
        if (!obd::obd().dtcClearAllowed(why)) { sp::message("Cancellazione non consentita", why); return; }
        sp::confirm("Cancella errori (1/2)", "Azzerare la memoria errori della centralina? La spia motore (MIL) si spegnera'.", "Continua", []() {
            sp::confirm("Conferma definitiva (2/2)",
                        "Verranno cancellati anche i freeze frame e azzerati i monitor di prontezza: la revisione puo' non passare "
                        "finche' non si completano di nuovo guidando.", "Cancella ora", []() {
                if (!async::runInWorker([]() {
                        string msg;
                        obd::obd().clearDtcCodes(msg);  // ricontrolla giri e velocita'
                        async::postToUi([msg]() { sp::message("Esito", msg); });
                    })) return;
                sp::message("Diagnostica", "Cancellazione in corso...");
            }, true);
        }, true);
    }, true);
    sp::rowInfo(l, []() { return string("La cancellazione e' consentita solo a motore spento e auto ferma, con il quadro acceso."); });
}

// =========================================================== DATA E ORA
void page_data() {
    lv_obj_t* l = sp::push("Data e ora");
    auto& rtc = storage::rtc();
    sp::section(l, "OROLOGIO");
    sp::rowValue(l, "Ora attuale", []() { return storage::rtc().getFormattedTime("%H:%M:%S"); });
    sp::rowValue(l, "Data", []() { return storage::rtc().getFormattedTime("%d/%m/%Y"); });
    sp::rowInfo(l, []() {
        auto& r = storage::rtc();
        return fmt("Chip: %s (0x%02X)\nBatteria tampone: %s", r.getChipName(), r.getI2cAddress(), r.isBatteryOk() ? "OK" : "da controllare");
    });
    (void)rtc;
    sp::section(l, "REGOLAZIONE");
    sp::rowStepper(l, "Ore", []() { return storage::rtc().getFormattedTime("%H"); },
                   []() { storage::rtc().adjustHours(-1); }, []() { storage::rtc().adjustHours(1); });
    sp::rowStepper(l, "Minuti", []() { return storage::rtc().getFormattedTime("%M"); },
                   []() { storage::rtc().adjustMinutes(-1); }, []() { storage::rtc().adjustMinutes(1); });
    sp::rowAction(l, "Azzera secondi (:00)", []() { storage::rtc().resetSeconds(); });
    sp::rowStepper(l, "Giorno", []() { return storage::rtc().getFormattedTime("%d"); },
                   []() { storage::rtc().adjustDays(-1); }, []() { storage::rtc().adjustDays(1); });
    sp::rowStepper(l, "Mese", []() { return storage::rtc().getFormattedTime("%m"); },
                   []() { storage::rtc().adjustMonths(-1); }, []() { storage::rtc().adjustMonths(1); });
    sp::rowStepper(l, "Anno", []() { return storage::rtc().getFormattedTime("%Y"); },
                   []() { storage::rtc().adjustYears(-1); }, []() { storage::rtc().adjustYears(1); });
    sp::section(l, "SINCRONIZZAZIONE");
    sp::rowAction(l, "Sincronizza via Wi-Fi (NTP)", []() {
        if (!wifi::WifiManager::instance().isConnected()) { sp::message("NTP", "Serve una rete Wi-Fi con accesso a internet."); return; }
        storage::rtc().enableNtpSync();
        sp::message("NTP", "Sincronizzazione avviata.");
    });
    sp::rowAction(l, "Ripristina data del firmware", []() {
        sp::confirm("Data e ora", "Imposta data e ora della compilazione del firmware?", "Ripristina", []() { storage::rtc().setTimeFromCompile(); }, false);
    });
}

// =========================================================== MICROSD
void page_sd() {
    lv_obj_t* l = sp::push("MicroSD e log");
    sp::section(l, "SCHEDA");
    sp::rowInfo(l, []() {
        auto& sd = storage::sd();
        if (!sd.isMounted()) return string("Scheda non montata.");
        return fmt("Capacit\xC3\xA0: %.1f GB\nLibero: %.1f GB\nFile di log: %u (%.1f MB)\nLog attivo: %s",
                   sd.getTotalSizeMB() / 1024.0, sd.getFreeSizeMB() / 1024.0, (unsigned)sd.getLogFileCount(),
                   sd.getLogFilesTotalBytes() / 1048576.0, sd.getActiveLogPath());
    });
    sp::rowAction(l, "Monta / rimonta la scheda", []() {
        if (!async::runInWorker([]() {
                storage::telemetry::pause();
                const bool ok = storage::sd().remount();
                storage::telemetry::resume();
                async::postToUi([ok]() { sp::message("MicroSD", ok ? "Scheda montata." : "Scheda non trovata o non leggibile."); });
            })) return;
        sp::message("MicroSD", "Montaggio in corso...");
    });
    sp::rowAction(l, "Test velocit\xC3\xA0", []() {
        if (!async::runInWorker([]() {
                float wr = 0, rd = 0;
                const bool ok = storage::sd().runBenchmark(&wr, &rd);
                async::postToUi([ok, wr, rd]() {
                    sp::message("Test MicroSD", ok ? fmt("Scrittura: %.1f MB/s\nLettura: %.1f MB/s", wr, rd) : string("Errore durante il test."));
                });
            })) return;
        sp::message("Test MicroSD", "Test in corso (512 KB)...");
    });
    sp::section(l, "TELEMETRIA");
    sp::rowAction(l, "Nuova sessione di log", []() {
        storage::telemetry::pause();
        const bool ok = storage::sd().startNewLogSession();
        storage::telemetry::resume();
        sp::message("Telemetria", ok ? fmt("Nuovo file: %s", storage::sd().getActiveLogPath()) : string("MicroSD non disponibile."));
    });
    sp::rowAction(l, "Svuota vecchi log", []() {
        sp::confirm("Svuota log", "Eliminare tutti i file di log CSV dalla MicroSD? L'operazione non si puo' annullare.", "Elimina", []() {
            storage::telemetry::pause();
            storage::sd().clearLogs();
            storage::telemetry::resume();
        }, true);
    }, true);
    sp::section(l, "AGGIORNAMENTO FIRMWARE");
    sp::rowAction(l, "Aggiorna da MicroSD", []() {
        struct stat st;
        const bool found = (stat("/sdcard/update.bin", &st) == 0 && st.st_size > 100000) ||
                           (stat("/sdcard/firmware.bin", &st) == 0 && st.st_size > 100000);
        if (!found) { sp::message("Aggiornamento", "Nessun file update.bin o firmware.bin valido nella radice della MicroSD."); return; }
        sp::confirm("Aggiornamento", "Trovato un firmware sulla MicroSD. Installarlo? Il display si riavviera' da solo. Non spegnere l'auto.", "Installa", []() {
            if (!async::runInWorker([]() {
                    const bool ok = ota::ota().checkAndApplySdUpdate(nullptr);  // riavvia da solo se va a buon fine
                    async::postToUi([ok]() { if (!ok) sp::message("Aggiornamento", "File non valido per questo display o errore di scrittura."); });
                })) return;
            sp::message("Aggiornamento", "Installazione in corso, non spegnere...");
        }, false);
    });
}

// =========================================================== WEB SERVER
void page_web() {
    lv_obj_t* l = sp::push("Web Server");
    sp::section(l, "WEB SERVER");
    sp::rowValue(l, "Stato", []() { return string(web::isRunning() ? "Attivo" : "Spento"); });
    sp::rowValue(l, "Indirizzo", []() { return web::getServerUrl(); });
    sp::rowAction(l, "Avvia / arresta", []() {
        if (web::isRunning()) { web::stopServer(); return; }
        if (wifi::WifiManager::instance().isConnected()) { start_web_server(); return; }
        // non collegato: si collega all'ultima rete "internet" salvata, altrimenti propone di cercarne una
        links::WifiEntry e;
        if (links::lastWifi(WifiRole::Internet, e)) {
            g_web_after_connect = true;
            wifi_connect_checked(e.ssid, e.pass, e.role);
            return;
        }
        sp::confirm("Web Server", "Il display non e' collegato a nessuna rete Wi-Fi. Vuoi cercarne una (casa o hotspot del telefono)?",
                    "Cerca reti", []() { g_web_after_connect = true; wifi_scan(); }, false);
    });
    sp::section(l, "RETE");
    sp::rowValue(l, "Collegato a", []() {
        auto& wm = wifi::WifiManager::instance();
        return wm.isConnected() ? wm.getCurrentSSID() : string("nessuna rete");
    });
    sp::rowAction(l, "Collega a una rete", []() { g_web_after_connect = true; wifi_scan(); });
    sp::rowInfo(l, []() {
        return string("Dal browser di telefono o PC puoi scaricare i log CSV e aggiornare il firmware. "
                      "Il server si spegne da solo dopo 10 minuti.");
    });
}

// =========================================================== SISTEMA
void page_sistema() {
    lv_obj_t* l = sp::push("Sistema");
    sp::section(l, "INFORMAZIONI");
    sp::rowInfo(l, []() {
        esp_chip_info_t ci;
        esp_chip_info(&ci);
        return fmt("ESP32-P4 rev %d \xC2\xB7 IDF %s\nSRAM libera: %u KB \xC2\xB7 PSRAM libera: %u KB\nPartizione attiva: %s\nFirmware: %s %s",
                   ci.revision, esp_get_idf_version(),
                   (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                   (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
                   ota::ota().getCurrentPartitionName().c_str(), __DATE__, __TIME__);
    });
    sp::rowInfo(l, []() {
        return fmt("Letture OBD: %lu ok \xC2\xB7 %lu errori\nFrame CAN: %lu \xC2\xB7 errori %lu\nTouch: %lu letture vuote corrette",
                   (unsigned long)obd::obd().successfulReads(), (unsigned long)obd::obd().failedReads(),
                   (unsigned long)can::can().totalRxFrames(), (unsigned long)can::can().totalErrors(),
                   (unsigned long)lvgl_port_touch_bridged_count());
    });
    sp::section(l, "STRUMENTI");
    sp::rowToggle(l, "Modalit\xC3\xA0 debug", []() { return current().debug_mode; }, [](bool v) { setDebugMode(v); });
    sp::rowToggle(l, "Simulazione dati", []() { return current().demo_mode && current().debug_mode; }, [](bool v) {
        if (!current().debug_mode) { sp::message("Simulazione", "Attiva prima la Modalit\xC3\xA0 debug."); sp::refresh(); return; }
        setDemoMode(v);
    });
    sp::rowAction(l, "Riavvia il display", []() {
        sp::confirm("Riavvio", "Riavviare il display adesso?", "Riavvia", []() { esp_restart(); }, false);
    });
    sp::section(l, "LICENZA");
    sp::rowInfo(l, []() {
        return string("Abarth Dashboard \xC2\xB7 (C) 2026 Emil. Tutti i diritti riservati.\n"
                      "Font Manrope: SIL Open Font License 1.1. Grafica LVGL (MIT).");
    });
}

// =========================================================== MANOMETRI CLASSICA
// (non piu' nell'elenco della UI Moderno: resta nelle Impostazioni della Classica)
[[maybe_unused]]
void page_manometri() {
    lv_obj_t* l = sp::push("Manometri (Classica)");
    sp::section(l, "SCHEDA PERSONALIZZATA");
    sp::rowToggle(l, "Mostra scheda Custom", []() { return isCustomTabEnabled(); }, [](bool v) { setCustomTabEnabled(v); });
    sp::section(l, "INDICATORI VISIBILI");
    size_t n = 0;
    const MetricInfo* cat = metricCatalog(n);
    for (size_t i = 0; i < n; ++i) {
        const auto m = cat[i].metric;
        sp::rowToggle(l, cat[i].long_name, [m]() { return isMetricVisible(m); }, [m](bool v) { setMetricVisible(m, v); });
    }
    sp::rowInfo(l, []() { return string("Valgono per le pagine della UI Classica. Nella UI Moderno usa la pagina PERSONALE."); });
}

struct Cat { const char* name; void (*open)(); };
const Cat kCats[] = {
    {"Collegamento", page_collegamento},
    {"CAN diretto", page_can},
    {"Schema collegamenti", page_schema},
    {"Display", page_display},
    {"Unit\xC3\xA0 di misura", page_unita},
    {"Allarmi", page_allarmi},
    {"Diagnostica ECU", page_dtc},
    {"Data e ora", page_data},
    {"MicroSD e log", page_sd},
    {"Web Server", page_web},
    {"Sistema", page_sistema},
};
}  // namespace

void build(lv_obj_t* host, int32_t w, int32_t h) {
    sp::init(host, w, h);
    g_root = sp::pushRoot();
    sp::section(g_root, "IMPOSTAZIONI");
    for (const auto& c : kCats) {
        sp::rowNav(g_root, c.name, nullptr, c.open);
    }
}

void update(int64_t now_ms) {
    static int64_t last = 0;
    if (now_ms - last >= 500) {
        last = now_ms;
        sp::refresh();
    }
}

bool inSubPage() { return sp::depth() > 0; }
void closeSubPages() { sp::popAll(); }
lv_obj_t* rootList() { return g_root; }
int debugDepth() { return sp::depth(); }
lv_obj_t* debugTopList() { return sp::topList(); }
void debugOpenPageOrder() { page_order_edit(); }
void debugWifiScan() { g_web_after_connect = false; wifi_scan(); }
void debugWebStart() {
    page_web();
    if (!web::isRunning() && !wifi::WifiManager::instance().isConnected()) {
        links::WifiEntry e;
        if (!links::lastWifi(WifiRole::Internet, e))
            sp::confirm("Web Server", "Il display non e' collegato a nessuna rete Wi-Fi. Vuoi cercarne una (casa o hotspot del telefono)?",
                        "Cerca reti", []() { g_web_after_connect = true; wifi_scan(); }, false);
    }
}
lv_obj_t* debugOrderButton(int pos, int which) {
    return (pos >= 0 && pos < page_order::kData && which >= 0 && which < 3) ? g_order_btns[pos][which] : nullptr;
}
int debugCategoryCount() { return static_cast<int>(sizeof(kCats) / sizeof(kCats[0])); }
bool debugScrollBottom() {
    lv_obj_t* l = sp::topList();
    if (!l || lv_obj_get_scroll_bottom(l) <= 0) return false;
    lv_obj_scroll_to_y(l, lv_obj_get_scroll_y(l) + lv_obj_get_scroll_bottom(l), LV_ANIM_OFF);
    return true;
}
void debugOpen(int n) {
    sp::popAll();
    if (n >= 0 && n < static_cast<int>(sizeof(kCats) / sizeof(kCats[0]))) kCats[n].open();
}

}  // namespace abarth::ui_v2::settings_pages
