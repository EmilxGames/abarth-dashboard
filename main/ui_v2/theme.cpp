#include "ui_v2/theme.h"
#include "config/settings.h"

namespace abarth::ui_v2::theme {

uint32_t accent() {
    const uint8_t i = settings::current().ui_accent;
    return kAccents[i <= 3 ? i : 0];
}

// Provvisori: verranno sostituiti dai font Manrope generati (fonts/)
const lv_font_t* fontHero()   { return &lv_font_montserrat_48; }
const lv_font_t* fontValue()  { return &lv_font_montserrat_48; }
const lv_font_t* fontMedium() { return &lv_font_montserrat_32; }
const lv_font_t* fontUnit()   { return &lv_font_montserrat_22; }
const lv_font_t* fontLabel()  { return &lv_font_montserrat_16; }
const lv_font_t* fontStatus() { return &lv_font_montserrat_18; }
const lv_font_t* fontBody()   { return &lv_font_montserrat_24; }

}  // namespace abarth::ui_v2::theme
