#include "ui_v2/theme.h"
#include "config/settings.h"

// Manrope (OFL), file C generati: dichiarati fuori dal namespace
extern "C" {
LV_FONT_DECLARE(manrope_hero_228);
LV_FONT_DECLARE(manrope_value_75);
LV_FONT_DECLARE(manrope_medium_32);
LV_FONT_DECLARE(manrope_unit_22);
LV_FONT_DECLARE(manrope_label_17);
LV_FONT_DECLARE(manrope_status_19);
LV_FONT_DECLARE(manrope_body_24);
}

namespace abarth::ui_v2::theme {

uint32_t accent() {
    const uint8_t i = settings::current().ui_accent;
    return kAccents[i <= 3 ? i : 0];
}

// Manrope (OFL), generati con tools/gen_fonts.sh da fonts/manrope

const lv_font_t* fontHero()   { return &manrope_hero_228; }
const lv_font_t* fontValue()  { return &manrope_value_75; }
const lv_font_t* fontMedium() { return &manrope_medium_32; }
const lv_font_t* fontUnit()   { return &manrope_unit_22; }
const lv_font_t* fontLabel()  { return &manrope_label_17; }
const lv_font_t* fontStatus() { return &manrope_status_19; }
const lv_font_t* fontBody()   { return &manrope_body_24; }

}  // namespace abarth::ui_v2::theme
