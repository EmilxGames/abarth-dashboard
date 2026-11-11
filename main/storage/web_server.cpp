#include "storage/web_server.h"

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <esp_log.h>
#include <esp_http_server.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "storage/ota_updater.h"
#include "storage/rtc_manager.h"
#include "storage/sd_card_manager.h"
#include "wifi_manager.h"

namespace abarth::web {

namespace {
constexpr const char* TAG = "web_server";
httpd_handle_t s_server = nullptr;
esp_timer_handle_t s_auto_stop_timer = nullptr;
constexpr uint64_t kAutoStopUs = 10ULL * 60ULL * 1000000ULL;  // 10 minuti
constexpr const char* kLogDir = "/sdcard/logs";
// Token anti-CSRF generato a ogni avvio del server: le richieste che modificano
// qualcosa (cancellazione, OTA) devono riportarlo.
char s_token[17] = {};

bool token_ok(const char* t) {
    return t && s_token[0] != '\0' && std::strcmp(t, s_token) == 0;
}

void auto_stop_cb(void*) {
    ESP_LOGI(TAG, "Timeout 10 minuti: arresto automatico Web Server");
    // httpd_stop non va chiamato dal task del timer se il server sta servendo
    // una richiesta lunga: lo facciamo da un task dedicato.
    xTaskCreate([](void*) { stopServer(); vTaskDelete(nullptr); }, "web_stop", 3072, nullptr, 3, nullptr);
}

// Helper per estrarre parametro da query string
bool get_query_param(const char* query, const char* key, char* out_val, size_t max_len) {
    if (!query || !key || !out_val || max_len == 0) return false;
    char full_key[64];
    snprintf(full_key, sizeof(full_key), "%s=", key);
    const char* p = strstr(query, full_key);
    if (!p) return false;
    p += strlen(full_key);
    size_t i = 0;
    while (*p && *p != '&' && i < max_len - 1) {
        out_val[i++] = *p++;
    }
    out_val[i] = '\0';
    return true;
}

// Handler GET / (Dashboard Web, Telemetria & OTA)
esp_err_t index_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");

    // Header pagina con stile Abarth Dark
    httpd_resp_sendstr_chunk(req,
        "<!DOCTYPE html><html lang='it'><head><meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Abarth 595 - Centro Controllo Dashboard</title>"
        "<style>"
        "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;"
        "background:#0d0d12;color:#f0f0f5;margin:0;padding:20px;}"
        ".container{max-width:850px;margin:0 auto;}"
        ".header{display:flex;align-items:center;justify-content:space-between;"
        "border-bottom:2px solid #E11D48;padding-bottom:15px;margin-bottom:25px;}"
        "h1{margin:0;color:#FFFFFF;font-size:24px;letter-spacing:1px;}"
        ".badge{background:#E11D48;color:#fff;padding:4px 10px;border-radius:12px;font-size:12px;font-weight:bold;}"
        ".card{background:#181822;border:1px solid #282838;border-radius:12px;padding:20px;margin-bottom:20px;box-shadow:0 4px 12px rgba(0,0,0,0.5);}"
        ".card-title{font-size:18px;font-weight:bold;color:#FFCC00;margin-top:0;margin-bottom:15px;display:flex;align-items:center;gap:8px;}"
        ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px;margin-bottom:15px;}"
        ".stat-box{background:#111118;padding:12px;border-radius:8px;border-left:3px solid #E11D48;}"
        ".stat-lbl{font-size:12px;color:#8E8E9E;text-transform:uppercase;}"
        ".stat-val{font-size:16px;font-weight:bold;color:#FFF;margin-top:4px;}"
        "table{width:100%;border-collapse:collapse;margin-top:10px;}"
        "th,td{padding:10px;text-align:left;border-bottom:1px solid #252535;font-size:14px;}"
        "th{color:#8E8E9E;font-size:12px;text-transform:uppercase;}"
        "a.btn,button.btn{display:inline-block;background:#E11D48;color:#FFF;text-decoration:none;"
        "padding:7px 14px;border-radius:6px;font-size:13px;font-weight:bold;border:none;cursor:pointer;transition:0.2s;}"
        "a.btn:hover,button.btn:hover{background:#ff2453;}"
        ".btn-del{background:#3a1818 !important;color:#ff6666 !important;margin-left:6px;}"
        ".btn-del:hover{background:#5a2020 !important;}"
        ".progress-bar{height:16px;background:#252535;border-radius:8px;overflow:hidden;margin-top:12px;display:none;}"
        ".progress-fill{height:100%;background:#00CC66;width:0%;transition:width 0.2s;}"
        "#status-msg{margin-top:10px;font-size:13px;color:#AAA;}"
        "</style></head><body><div class='container'>");

    // Token per le richieste che modificano lo stato
    char tok_chunk[96];
    snprintf(tok_chunk, sizeof(tok_chunk), "<script>document.body.dataset.token='%s';</script>", s_token);
    httpd_resp_sendstr_chunk(req, tok_chunk);

    // Header Abarth
    httpd_resp_sendstr_chunk(req,
        "<div class='header'><div><h1>ABARTH 595</h1>"
        "<div style='color:#8E8E9E;font-size:13px;margin-top:2px;'>CENTRO CONTROLLO TELEMETRIA & FIRMWARE</div></div>"
        "<span class='badge'>ESP32-P4 ONLINE</span></div>");

    // Card 1: Stato Sistema, RTC & Partizione
    char stat_chunk[1024];
    snprintf(stat_chunk, sizeof(stat_chunk),
        "<div class='card'><div class='card-title'>Stato Hardware & Sistema</div>"
        "<div class='grid'>"
        "<div class='stat-box'><div class='stat-lbl'>Data e Ora RTC</div><div class='stat-val'>%s</div></div>"
        "<div class='stat-box'><div class='stat-lbl'>MicroSD Storage</div><div class='stat-val'>%.1f / %.1f GB</div></div>"
        "<div class='stat-box'><div class='stat-lbl'>Partizione Attiva</div><div class='stat-val'>%s</div></div>"
        "<div class='stat-box'><div class='stat-lbl'>Batteria RTC</div><div class='stat-val'>%s</div></div>"
        "</div></div>",
        storage::rtc().getFormattedTime().c_str(),
        storage::sd().isMounted() ? (storage::sd().getTotalSizeMB() - storage::sd().getFreeSizeMB()) / 1024.0f : 0.0f,
        storage::sd().isMounted() ? storage::sd().getTotalSizeMB() / 1024.0f : 0.0f,
        ota::ota().getCurrentPartitionName().c_str(),
        storage::rtc().isBatteryOk() ? "OK (3.0V)" : "Scarica"
    );
    httpd_resp_sendstr_chunk(req, stat_chunk);

    // Card 2: Telemetria CSV MicroSD
    httpd_resp_sendstr_chunk(req,
        "<div class='card'><div class='card-title'>Sessioni Telemetria Registrate (MicroSD)</div>"
        "<p style='color:#AAA;font-size:13px;margin:0 0 10px 0;'>Scarica i file di telemetria per analizzarli con Excel, MegaLogViewer o software grafici:</p>");

    DIR* d = opendir(kLogDir);
    if (!d) {
        httpd_resp_sendstr_chunk(req, "<p style='color:#8E8E9E;font-size:13px;'>Nessuna cartella di telemetria trovata o MicroSD non montata.</p>");
    } else {
        httpd_resp_sendstr_chunk(req,
            "<table><thead><tr><th>Nome File</th><th>Dimensione</th><th>Azioni</th></tr></thead><tbody>");
        
        struct dirent* entry = nullptr;
        int file_count = 0;
        while ((entry = readdir(d)) != nullptr) {
            if (entry->d_name[0] == '.') continue;
            const char* ext = strrchr(entry->d_name, '.');
            if (!ext || (strcmp(ext, ".csv") != 0 && strcmp(ext, ".CSV") != 0)) continue;

            char filepath[320];
            snprintf(filepath, sizeof(filepath), "%s/%s", kLogDir, entry->d_name);
            struct stat st;
            size_t fsize = (stat(filepath, &st) == 0) ? st.st_size : 0;

            char row[1600];
            snprintf(row, sizeof(row),
                "<tr><td><strong>%s</strong></td><td>%.1f KB</td>"
                "<td><a class='btn' href='/telemetry/download?file=%s'>Scarica</a>"
                "<form method='post' action='/telemetry/delete' style='display:inline' onsubmit='return confirm(\"Eliminare questo log?\");'>"
                "<input type='hidden' name='file' value='%s'><input type='hidden' name='token' value='%s'>"
                "<button class='btn btn-del' type='submit'>Elimina</button></form></td></tr>",
                entry->d_name, fsize / 1024.0f, entry->d_name, entry->d_name, s_token
            );
            httpd_resp_sendstr_chunk(req, row);
            file_count++;
        }
        closedir(d);

        if (file_count == 0) {
            httpd_resp_sendstr_chunk(req, "<tr><td colspan='3' style='color:#8E8E9E;'>Nessuna sessione CSV presente. Verranno generate durante la guida.</td></tr>");
        }
        httpd_resp_sendstr_chunk(req, "</tbody></table>");
    }
    httpd_resp_sendstr_chunk(req, "</div>");

    // Card 3: Aggiornamento Firmware OTA (Over-The-Air)
    char ota_card[1024];
    snprintf(ota_card, sizeof(ota_card),
        "<div class='card'><div class='card-title'>Aggiornamento Firmware Wireless (OTA)</div>"
        "<p style='color:#AAA;font-size:13px;margin:0 0 15px 0;'>"
        "Seleziona il file compilato <code>abarth-dashboard.bin</code> per installarlo senza cavi. "
        "Partizione di destinazione: <strong>%s</strong>.</p>"
        "<input type='file' id='firmware-file' accept='.bin' style='font-size:13px;color:#ccc;margin-bottom:12px;'><br>"
        "<button class='btn' onclick='uploadFirmware()'>Carica & Installa Firmware</button>"
        "<div class='progress-bar' id='pbar'><div class='progress-fill' id='pfill'></div></div>"
        "<div id='status-msg'></div>"
        "</div>",
        ota::ota().getNextPartitionName().c_str()
    );
    httpd_resp_sendstr_chunk(req, ota_card);

    // Javascript per upload asincrono con barra di avanzamento
    httpd_resp_sendstr_chunk(req,
        "<script>"
        "function uploadFirmware(){"
        "  var fileInput = document.getElementById('firmware-file');"
        "  if(!fileInput.files.length){ alert('Seleziona un file .bin!'); return; }"
        "  var file = fileInput.files[0];"
        "  var pbar = document.getElementById('pbar');"
        "  var pfill = document.getElementById('pfill');"
        "  var smsg = document.getElementById('status-msg');"
        "  pbar.style.display = 'block';"
        "  smsg.innerText = 'Inizio caricamento firmware...';"
        "  var xhr = new XMLHttpRequest();"
        "  xhr.open('POST', '/update', true);"
        "  xhr.setRequestHeader('X-Token', document.body.dataset.token);"
        "  xhr.upload.onprogress = function(e){"
        "    if(e.lengthComputable){"
        "      var pct = Math.round((e.loaded / e.total) * 100);"
        "      pfill.style.width = pct + '%';"
        "      smsg.innerText = 'Installazione in corso: ' + pct + '% (' + Math.round(e.loaded/1024) + ' KB / ' + Math.round(e.total/1024) + ' KB)';"
        "    }"
        "  };"
        "  xhr.onload = function(){"
        "    if(xhr.status == 200){"
        "      pfill.style.width = '100%';"
        "      smsg.innerHTML = '<strong style=\"color:#00CC66\">Firmware installato con successo! Riavvio della Dashboard in corso...</strong>';"
        "      setTimeout(function(){ window.location.reload(); }, 6000);"
        "    } else {"
        "      smsg.innerHTML = '<strong style=\"color:#FF3333\">Errore durante l\\'aggiornamento: ' + xhr.responseText + '</strong>';"
        "    }"
        "  };"
        "  xhr.onerror = function(){ smsg.innerHTML = '<strong style=\"color:#FF3333\">Errore di connessione durante l\\'upload!</strong>'; };"
        "  xhr.send(file);"
        "}"
        "</script></div></body></html>"
    );

    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

// Handler GET /telemetry/download?file=...
esp_err_t download_handler(httpd_req_t *req) {
    char query[256] = {};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Parametro mancante");
        return ESP_FAIL;
    }

    char filename[128] = {};
    if (!get_query_param(query, "file", filename, sizeof(filename))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File non specificato");
        return ESP_FAIL;
    }

    // Protezione contro path traversal
    if (strstr(filename, "..") || strchr(filename, '/') || strchr(filename, '\\')) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Accesso negato");
        return ESP_FAIL;
    }

    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/%s", kLogDir, filename);

    FILE* f = fopen(filepath, "rb");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File non trovato");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/csv");
    char disp_hdr[256];
    snprintf(disp_hdr, sizeof(disp_hdr), "attachment; filename=\"%s\"", filename);
    httpd_resp_set_hdr(req, "Content-Disposition", disp_hdr);

    char chunk[4096];
    size_t read_bytes = 0;
    while ((read_bytes = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        if (httpd_resp_send_chunk(req, chunk, read_bytes) != ESP_OK) {
            fclose(f);
            return ESP_FAIL;
        }
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

// Handler POST /telemetry/delete (body: file=...&token=...)
esp_err_t delete_handler(httpd_req_t *req) {
    char body[256] = {};
    if (req->content_len == 0 || req->content_len >= sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Richiesta non valida");
        return ESP_FAIL;
    }
    size_t got = 0;
    while (got < req->content_len) {
        int r = httpd_req_recv(req, body + got, req->content_len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) return ESP_FAIL;
        got += r;
    }
    body[got] = '\0';

    char token[24] = {};
    if (!get_query_param(body, "token", token, sizeof(token)) || !token_ok(token)) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Token non valido: ricarica la pagina");
        return ESP_FAIL;
    }
    char filename[128] = {};
    if (get_query_param(body, "file", filename, sizeof(filename))) {
        if (!strstr(filename, "..") && !strchr(filename, '/') && !strchr(filename, '\\') && !strchr(filename, '%')) {
            char filepath[256];
            snprintf(filepath, sizeof(filepath), "%s/%s", kLogDir, filename);
            if (unlink(filepath) == 0) {
                ESP_LOGI(TAG, "Cancellato file telemetria: %s", filepath);
            } else {
                ESP_LOGW(TAG, "Cancellazione fallita: %s", filepath);
            }
        }
    }

    // Reindirizza alla home
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// Handler POST /update (Web OTA)
esp_err_t update_post_handler(httpd_req_t *req) {
    char token[24] = {};
    if (httpd_req_get_hdr_value_str(req, "X-Token", token, sizeof(token)) != ESP_OK || !token_ok(token)) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Token non valido: ricarica la pagina");
        return ESP_FAIL;
    }
    size_t total_size = req->content_len;
    if (total_size == 0 || total_size > 4 * 1024 * 1024) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Dimensione firmware non valida");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Ricezione firmware Web OTA (%zu bytes)...", total_size);

    esp_err_t err = ota::ota().beginWebOta(total_size);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Inizializzazione partizione OTA fallita");
        return ESP_FAIL;
    }

    char* buf = (char*)malloc(4096);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memoria insufficiente");
        return ESP_FAIL;
    }

    size_t received = 0;
    while (received < total_size) {
        int ret = httpd_req_recv(req, buf, 4096);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
            free(buf);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Interruzione ricezione dati");
            return ESP_FAIL;
        }

        err = ota::ota().writeWebOtaChunk(buf, ret);
        if (err != ESP_OK) {
            free(buf);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                err == ESP_ERR_INVALID_VERSION ? "File non valido per questo dispositivo"
                                                               : "Errore scrittura flash OTA");
            return ESP_FAIL;
        }
        received += ret;
    }
    free(buf);

    err = ota::ota().endWebOta();
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Errore verifica firmware");
        return ESP_FAIL;
    }

    httpd_resp_sendstr(req, "OK");

    // Task asincrono per riavvio dopo aver risposto al client
    xTaskCreate([](void*){
        vTaskDelay(pdMS_TO_TICKS(1500));
        esp_restart();
    }, "reboot_task", 2048, nullptr, 5, nullptr);

    return ESP_OK;
}

}  // namespace

esp_err_t startServer() {
    if (s_server != nullptr) {
        return ESP_OK;
    }
    // Senza rete lo stack TCP/IP puo' non essere inizializzato: httpd_start
    // andrebbe in assert (Invalid mbox) e riavvierebbe il display.
    if (!wifi::WifiManager::instance().isConnected()) {
        ESP_LOGW(TAG, "Web Server non avviato: WiFi non connesso");
        return ESP_ERR_INVALID_STATE;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32768;
    config.max_open_sockets = 4;
    config.stack_size = 8192;
    config.lru_purge_enable = true;

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Errore avvio HTTP server: %s", esp_err_to_name(ret));
        s_server = nullptr;
        return ret;
    }

    snprintf(s_token, sizeof(s_token), "%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random());

    if (!s_auto_stop_timer) {
        esp_timer_create_args_t targs = {};
        targs.callback = &auto_stop_cb;
        targs.name = "web_autostop";
        esp_timer_create(&targs, &s_auto_stop_timer);
    }
    if (s_auto_stop_timer) {
        esp_timer_stop(s_auto_stop_timer);
        esp_timer_start_once(s_auto_stop_timer, kAutoStopUs);
    }

    // Registrazione rotte
    httpd_uri_t uri_index = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = index_get_handler,
        .user_ctx  = nullptr
    };
    httpd_register_uri_handler(s_server, &uri_index);

    httpd_uri_t uri_download = {
        .uri       = "/telemetry/download",
        .method    = HTTP_GET,
        .handler   = download_handler,
        .user_ctx  = nullptr
    };
    httpd_register_uri_handler(s_server, &uri_download);

    httpd_uri_t uri_delete = {
        .uri       = "/telemetry/delete",
        .method    = HTTP_POST,
        .handler   = delete_handler,
        .user_ctx  = nullptr
    };
    httpd_register_uri_handler(s_server, &uri_delete);

    httpd_uri_t uri_update = {
        .uri       = "/update",
        .method    = HTTP_POST,
        .handler   = update_post_handler,
        .user_ctx  = nullptr
    };
    httpd_register_uri_handler(s_server, &uri_update);

    ESP_LOGI(TAG, "Web Server Abarth avviato su porta 80 (arresto automatico tra 10 minuti)");
    return ESP_OK;
}

void stopServer() {
    if (s_auto_stop_timer) esp_timer_stop(s_auto_stop_timer);
    s_token[0] = '\0';
    if (s_server) {
        httpd_stop(s_server);
        s_server = nullptr;
        ESP_LOGI(TAG, "Web Server Abarth fermato");
    }
}

bool isRunning() {
    return s_server != nullptr;
}

std::string getServerUrl() {
    if (!isRunning()) return "Disattivato";
    std::string ip = wifi::WifiManager::instance().getCurrentIP();
    if (ip.empty()) return "In attesa di IP...";
    return "http://" + ip;
}

}  // namespace abarth::web
