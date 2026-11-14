#include "ui_v2/background.h"

#include <cmath>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include "lvgl.h"
#include "src/misc/cache/instance/lv_image_cache.h"

#include "config/settings.h"
#include "ui_v2/theme.h"

extern "C" {   // ui_v2/logo_masks.c (generato da tools/logo_masks.swift dalle immagini in custom/assets)
extern const uint16_t logo_scorpion_w, logo_scorpion_h;
extern const uint8_t logo_scorpion_map[];
extern const uint16_t logo_outline_w, logo_outline_h;
extern const uint8_t logo_outline_map[];
extern const uint16_t logo_word_w, logo_word_h;
extern const uint8_t logo_word_map[];
}

namespace abarth::ui_v2::background {

const char* const kNames[Count] = {"Nessuno", "Carbonio", "Luce soffusa", "Linee racing",
                                   "Scorpione", "Scorpione contorno", "Scritta ABARTH"};

namespace {
constexpr const char* TAG = "sfondo";
lv_obj_t* g_img = nullptr;
uint16_t* g_buf = nullptr;
lv_image_dsc_t g_dsc;
// Loghi: le schede sono opache e coprono quasi tutto lo schermo, quindi un
// logo nello sfondo si vedrebbe solo negli spazi tra le schede. Il logo e'
// invece una filigrana sopra a tutto (ARGB, solo un velo chiaro di pochi %),
// che non intercetta i tocchi.
lv_obj_t* g_ovl = nullptr;
uint32_t* g_ovl_buf = nullptr;
lv_image_dsc_t g_ovl_dsc;
int32_t g_w = 0, g_h = 0;

// colore di base = sfondo Moderno (#0A0B0E)
constexpr float kR = 0x0A, kG = 0x0B, kB = 0x0E;

/// Bayer 4x4: sfuma le differenze piccolissime di luminosita' che in RGB565
/// altrimenti farebbero bande
constexpr uint8_t kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

inline uint16_t pack(float l, int x, int y) {
    // l = variazione di luminosita' (livelli su 255) rispetto alla base
    const float d = (kBayer[y & 3][x & 3] + 0.5f) / 16.0f;
    auto q = [&](float v, float step) {
        float s = v / step + d;
        if (s < 0) s = 0;
        return static_cast<int>(s);
    };
    int r = q(kR + l, 8.0f), g = q(kG + l, 4.0f), b = q(kB + l * 1.05f, 8.0f);
    if (r > 31) r = 31;
    if (g > 63) g = 63;
    if (b > 31) b = 31;
    return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

float carbon(int x, int y) {
    // trama in diagonale (twill) a celle di 7 px, con una leggera sfumatura in ogni cella
    constexpr int c = 7;
    const int cx = x / c, cy = y / c;
    const bool dir = ((cx + cy) & 1) != 0;
    const float t = dir ? (x % c) / float(c) : (y % c) / float(c);
    return (dir ? 3.5f : 1.0f) + 2.2f * std::sin(t * 3.14159f) - 1.5f;
}

float vignette(int x, int y) {
    // luce soffusa al centro, bordi piu' scuri
    const float dx = (x - g_w * 0.5f) / (g_w * 0.75f), dy = (y - g_h * 0.42f) / (g_h * 0.62f);
    const float r2 = dx * dx + dy * dy;
    return 9.0f * std::exp(-r2 * 1.6f) - 3.0f;
}

float lines(int x, int y) {
    // tre linee sottili in diagonale nell'angolo in basso a destra
    float l = 0;
    const float u = (x + y) / 1.4142f;   // distanza lungo la diagonale
    const float base = (g_w + g_h) / 1.4142f * 0.80f;
    for (int i = 0; i < 3; ++i) {
        const float d = std::fabs(u - (base + i * 22.0f));
        const float w = i == 1 ? 3.0f : 1.6f;
        if (d < w + 1.5f) l += (d < w ? 9.0f : 9.0f * (w + 1.5f - d) / 1.5f);
    }
    // sfumano verso l'alto a sinistra
    const float fade = std::fmin(1.0f, std::fmax(0.0f, (x + y - g_w * 0.9f) / (g_h * 0.6f)));
    return l * fade;
}

/// Filigrana del logo: bianco con trasparenza; riempimento leggero e bordo in
/// alto a sinistra un po' piu' chiaro (effetto rilievo). l in livelli su 255
/// sopra alle schede scure (~#14161B): alfa = l / 235
bool render_overlay(const uint8_t* m, int mw, int mh, float fill, float edge) {
    if (!g_ovl_buf) g_ovl_buf = static_cast<uint32_t*>(heap_caps_malloc(mw * mh * 4, MALLOC_CAP_SPIRAM));
    if (!g_ovl_buf) return false;
    auto at = [&](int x, int y) -> float {
        if (x < 0 || y < 0 || x >= mw || y >= mh) return 0.0f;
        return m[y * mw + x] / 255.0f;
    };
    for (int y = 0; y < mh; ++y) {
        for (int x = 0; x < mw; ++x) {
            const float a = at(x, y);
            const float rel = at(x - 2, y - 2) - at(x + 2, y + 2);
            float l = a * fill + (rel > 0 ? rel * edge : rel * edge * 0.35f);
            if (l < 0) l = 0;
            // LVGL fonde in RGB565 con 32 livelli di trasparenza e il verde ha
            // gradini piu' fini: un velo bianco troppo leggero alza solo il
            // verde (logo verdastro). Misurato sul display: con alfa 16 rosso,
            // verde e blu salgono insieme (~4%), con 24 di ~7% e il grigio resta
            // neutro. Quindi solo 0 / 16 / 24, con dithering ordinato per i bordi.
            const float steps = l / 8.0f + (kBayer[y & 3][x & 3] + 0.5f) / 16.0f - 0.5f;
            const int st = steps < 0 ? 0 : static_cast<int>(steps);
            const int al = st == 0 ? 0 : st == 1 ? 16 : 24;
            g_ovl_buf[y * mw + x] = (static_cast<uint32_t>(al) << 24) | 0x00FFFFFFu;   // ARGB8888
        }
    }
    std::memset(&g_ovl_dsc, 0, sizeof(g_ovl_dsc));
    g_ovl_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    g_ovl_dsc.header.cf = LV_COLOR_FORMAT_ARGB8888;
    g_ovl_dsc.header.w = mw;
    g_ovl_dsc.header.h = mh;
    g_ovl_dsc.header.stride = mw * 4;
    g_ovl_dsc.data_size = mw * mh * 4;
    g_ovl_dsc.data = reinterpret_cast<const uint8_t*>(g_ovl_buf);
    return true;
}

void hide_overlay() {
    if (!g_ovl) return;
    lv_obj_add_flag(g_ovl, LV_OBJ_FLAG_HIDDEN);
    if (g_ovl_buf) {
        lv_image_set_src(g_ovl, nullptr);
        heap_caps_free(g_ovl_buf);
        g_ovl_buf = nullptr;
    }
}

void show_overlay(uint8_t style) {
    const uint8_t* m = logo_scorpion_map;
    int mw = logo_scorpion_w, mh = logo_scorpion_h;
    float fill = 8.5f, edge = 9.0f;   // riempimento ~4%, bordo illuminato ~7%
    if (style == ScorpionOutline) { m = logo_outline_map; mw = logo_outline_w; mh = logo_outline_h; fill = 12.0f; edge = 4.0f; }
    if (style == Wordmark) { m = logo_word_map; mw = logo_word_w; mh = logo_word_h; }
    hide_overlay();   // dimensioni diverse: buffer nuovo
    if (!render_overlay(m, mw, mh, fill, edge)) return;
    lv_image_cache_drop(&g_ovl_dsc);
    lv_image_set_src(g_ovl, &g_ovl_dsc);
    lv_obj_set_pos(g_ovl, (g_w - mw) / 2, (g_h - mh) / 2 + 20);
    lv_obj_move_foreground(g_ovl);
    lv_obj_remove_flag(g_ovl, LV_OBJ_FLAG_HIDDEN);
}

void render(uint8_t style) {
    const int64_t t0 = esp_timer_get_time();
    // luminosita' per pixel in PSRAM temporanea (float), poi conversione con dithering
    float* lum = static_cast<float*>(heap_caps_calloc(g_w * g_h, sizeof(float), MALLOC_CAP_SPIRAM));
    if (!lum) { ESP_LOGE(TAG, "memoria insufficiente"); return; }
    for (int y = 0; y < g_h; ++y) {
        for (int x = 0; x < g_w; ++x) {
            float l = 0;
            switch (style) {
                case Carbon: l = carbon(x, y); break;
                case Vignette: l = vignette(x, y); break;
                case Lines: l = lines(x, y); break;
                default: l = vignette(x, y) * 0.6f; break;   // sotto ai loghi una luce leggerissima
            }
            lum[y * g_w + x] = l;
        }
    }
    for (int y = 0; y < g_h; ++y)
        for (int x = 0; x < g_w; ++x) g_buf[y * g_w + x] = pack(lum[y * g_w + x], x, y);
    heap_caps_free(lum);
    ESP_LOGI(TAG, "sfondo \"%s\" disegnato in %lld ms", kNames[style], (long long)((esp_timer_get_time() - t0) / 1000));
}
}  // namespace

void create(lv_obj_t* scr) {
    g_w = lv_obj_get_width(scr);
    g_h = lv_obj_get_height(scr);
    g_img = lv_image_create(scr);
    lv_obj_move_to_index(g_img, 0);   // sotto a tutto
    lv_obj_set_pos(g_img, 0, 0);
    lv_obj_remove_flag(g_img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_img, LV_OBJ_FLAG_HIDDEN);
    g_ovl = lv_image_create(scr);   // filigrana dei loghi, sopra a tutto
    lv_obj_remove_flag(g_ovl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(g_ovl, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_ovl, LV_OBJ_FLAG_HIDDEN);
    apply(settings::current().ui_background);
}

void apply(uint8_t style) {
    if (!g_img) return;
    if (style >= Scorpion && style < Count) show_overlay(style);
    else hide_overlay();
    if (style == None || style >= Count) {
        lv_obj_add_flag(g_img, LV_OBJ_FLAG_HIDDEN);
        if (g_buf) {   // libera la PSRAM
            lv_image_set_src(g_img, nullptr);
            heap_caps_free(g_buf);
            g_buf = nullptr;
        }
        return;
    }
    if (!g_buf) {
        g_buf = static_cast<uint16_t*>(heap_caps_malloc(g_w * g_h * 2, MALLOC_CAP_SPIRAM));
        if (!g_buf) { ESP_LOGE(TAG, "memoria insufficiente per lo sfondo"); return; }
    }
    render(style);
    std::memset(&g_dsc, 0, sizeof(g_dsc));
    g_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    g_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    g_dsc.header.w = g_w;
    g_dsc.header.h = g_h;
    g_dsc.header.stride = g_w * 2;
    g_dsc.data_size = g_w * g_h * 2;
    g_dsc.data = reinterpret_cast<const uint8_t*>(g_buf);
    lv_image_cache_drop(&g_dsc);
    lv_image_set_src(g_img, &g_dsc);
    lv_obj_remove_flag(g_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(g_img);
}

}  // namespace abarth::ui_v2::background
