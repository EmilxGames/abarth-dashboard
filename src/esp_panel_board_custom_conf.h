/*
 * SPDX-FileCopyrightText: 2023-2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-FileCopyrightText: 2025 Abarth Dashboard contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/**
 * @file   esp_panel_board_custom_conf.h
 * @brief  Configurazione custom per il display GUITION JC1060P470C-I-W-Y
 *
 * Parametri derivati dallo schematico ufficiale JC1060P470C_I_W_Y V1.0 e dal
 * driver ESPHome "JC1060P470" (init sequence del pannello JD9165 a 1024x600).
 *
 * Hardware:
 *   - ESP32-P4 + ESP32-C6-MINI-1U (ESP-Hosted)
 *   - LCD 7" IPS 1024x600 MIPI-DSI 2 lane, driver JD9165
 *   - Touch capacitivo GT911 su I2C
 *   - Backlight PWM tramite MP3202 driver + pin di enable LCD_PWM
 */

#pragma once

// *INDENT-OFF*

/**
 * @brief Abilita la configurazione custom.
 *
 * Usiamo `#ifndef` così il build flag `-DESP_PANEL_BOARD_DEFAULT_USE_CUSTOM=1`
 * nell'env `abarth-p4-guition-custom` viene rispettato senza warning di
 * ridefinizione. Quando si compila l'env `abarth-p4-funcboard` questo blocco
 * resta disattivato.
 */
#ifndef ESP_PANEL_BOARD_DEFAULT_USE_CUSTOM
#define ESP_PANEL_BOARD_DEFAULT_USE_CUSTOM  (0)
#endif

#if ESP_PANEL_BOARD_DEFAULT_USE_CUSTOM
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////// Parametri generali //////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define ESP_PANEL_BOARD_NAME                "GUITION:JC1060P470C"

// Risoluzione nativa del pannello (orientamento landscape).
#define ESP_PANEL_BOARD_WIDTH               (1024)
#define ESP_PANEL_BOARD_HEIGHT              (600)

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////// Pannello LCD (MIPI-DSI) //////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define ESP_PANEL_BOARD_USE_LCD             (1)

#if ESP_PANEL_BOARD_USE_LCD
// Il display Guition monta un driver JD9165 (compatibile JD9165A).
#define ESP_PANEL_BOARD_LCD_CONTROLLER      JD9165

// Solo ESP32-P4 supporta MIPI-DSI.
#define ESP_PANEL_BOARD_LCD_BUS_TYPE        (ESP_PANEL_BUS_TYPE_MIPI_DSI)

    // -------------------- Host MIPI-DSI --------------------
    #define ESP_PANEL_BOARD_LCD_MIPI_DSI_LANE_NUM           (2)
    // Bit rate per lane. 750 Mbps e' il valore usato dal driver ESPHome
    // per questa combinazione JD9165 + JC1060P470 e garantisce margine
    // sufficiente con PCLK 54 MHz in RGB565.
    #define ESP_PANEL_BOARD_LCD_MIPI_DSI_LANE_RATE_MBPS     (750)

    // -------------------- Refresh panel (DPI) --------------------
    // 52 MHz: valore del demo ufficiale Guition (JD9165_1024_600_PANEL_60HZ).
    #define ESP_PANEL_BOARD_LCD_MIPI_DPI_CLK_MHZ            (52)
    #define ESP_PANEL_BOARD_LCD_MIPI_DPI_PIXEL_BITS         (ESP_PANEL_LCD_COLOR_BITS_RGB565)
    // Timing JD9165 @1024x600 dal driver ESPHome "JC1060P470".
    // hsync_pulse_width 20 (override ESPHome) per stabilita' sul Guition.
    #define ESP_PANEL_BOARD_LCD_MIPI_DPI_HPW                (20)
    #define ESP_PANEL_BOARD_LCD_MIPI_DPI_HBP                (160)
    #define ESP_PANEL_BOARD_LCD_MIPI_DPI_HFP                (160)
    #define ESP_PANEL_BOARD_LCD_MIPI_DPI_VPW                (10)
    #define ESP_PANEL_BOARD_LCD_MIPI_DPI_VBP                (23)
    #define ESP_PANEL_BOARD_LCD_MIPI_DPI_VFP                (12)

    // -------------------- DSI power PHY --------------------
    // LDO ID interno a ESP32-P4 per alimentare il PHY DSI (fisso a 3).
    #define ESP_PANEL_BOARD_LCD_MIPI_PHY_LDO_ID             (3)

/**
 * @brief Sequenza di inizializzazione del driver JD9165 per pannello
 * JC1060P470C-I-W-Y. Presa dal modello ESPHome `guition.py::JC1060P470`
 * (compatibile con il codice Arduino_GFX segnalato dal produttore).
 *
 * La sequenza include anche i tre comandi finali del demo ufficiale
 * (pixel format 0x3A, sleep-out 0x11, display-on 0x29): il driver
 * `esp_lcd_jd9165` della libreria ESP32_Display_Panel non li aggiunge
 * automaticamente quando si passa una vendor init sequence custom.
 */
#define ESP_PANEL_BOARD_LCD_VENDOR_INIT_CMD() \
    { \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x30, {0x00}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0xF7, {0x49, 0x61, 0x02, 0x00}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x30, {0x01}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x04, {0x0C}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x05, {0x00}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x06, {0x00}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x0B, {0x11}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x17, {0x00}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x20, {0x04}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x1F, {0x05}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x23, {0x00}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x25, {0x19}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x28, {0x18}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x29, {0x04}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x2A, {0x01}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x2B, {0x04}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x2C, {0x01}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x30, {0x02}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x01, {0x22}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x03, {0x12}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x04, {0x00}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x05, {0x64}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x0A, {0x08}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x0B, {0x0A, 0x1A, 0x0B, 0x0D, 0x0D, 0x11, 0x10, 0x06, 0x08, 0x1F, 0x1D}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x0C, {0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x0D, {0x16, 0x1B, 0x0B, 0x0D, 0x0D, 0x11, 0x10, 0x07, 0x09, 0x1E, 0x1C}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x0E, {0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x0F, {0x16, 0x1B, 0x0D, 0x0B, 0x0D, 0x11, 0x10, 0x1C, 0x1E, 0x09, 0x07}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x10, {0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x11, {0x0A, 0x1A, 0x0D, 0x0B, 0x0D, 0x11, 0x10, 0x1D, 0x1F, 0x08, 0x06}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x12, {0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x14, {0x00, 0x00, 0x11, 0x11}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x18, {0x99}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x30, {0x06}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x12, {0x36, 0x2C, 0x2E, 0x3C, 0x38, 0x35, 0x35, 0x32, 0x2E, 0x1D, 0x2B, 0x21, 0x16, 0x29}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x13, {0x36, 0x2C, 0x2E, 0x3C, 0x38, 0x35, 0x35, 0x32, 0x2E, 0x1D, 0x2B, 0x21, 0x16, 0x29}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x30, {0x0A}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x02, {0x4F}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x0B, {0x40}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x12, {0x3E}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x13, {0x78}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x30, {0x0D}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x0D, {0x04}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x10, {0x0C}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x11, {0x0C}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x12, {0x0C}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x13, {0x0C}), \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x30, {0x00}), \
        /* Pixel format 16bpp (RGB565), sleep-out, display-on. Questi tre
         * comandi sono necessari: il driver `esp_lcd_jd9165` di ESP32_Display_Panel,
         * a differenza del demo Guition, non li aggiunge automaticamente. */ \
        ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0,   0x3A, {0x55}), \
        ESP_PANEL_LCD_CMD_WITH_NONE_PARAM(120, 0x11), \
        ESP_PANEL_LCD_CMD_WITH_NONE_PARAM(20,  0x29), \
    }

// Formato colori e trasformazioni.
#define ESP_PANEL_BOARD_LCD_COLOR_BITS          (ESP_PANEL_LCD_COLOR_BITS_RGB565)
#define ESP_PANEL_BOARD_LCD_COLOR_BGR_ORDER     (0)     // RGB
#define ESP_PANEL_BOARD_LCD_COLOR_INEVRT_BIT    (0)

#define ESP_PANEL_BOARD_LCD_SWAP_XY             (0)
#define ESP_PANEL_BOARD_LCD_MIRROR_X            (0)
#define ESP_PANEL_BOARD_LCD_MIRROR_Y            (0)
#define ESP_PANEL_BOARD_LCD_GAP_X               (0)
#define ESP_PANEL_BOARD_LCD_GAP_Y               (0)

// Reset del pannello: schematico FPC1 pin 2 "LCD_RST" -> GPIO5 del ESP32-P4.
#define ESP_PANEL_BOARD_LCD_RST_IO              (5)
#define ESP_PANEL_BOARD_LCD_RST_LEVEL           (0)     // Attivo basso

#endif // ESP_PANEL_BOARD_USE_LCD

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////// Touch capacitivo (GT911) ///////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define ESP_PANEL_BOARD_USE_TOUCH               (1)

#if ESP_PANEL_BOARD_USE_TOUCH
#define ESP_PANEL_BOARD_TOUCH_CONTROLLER        GT911
#define ESP_PANEL_BOARD_TOUCH_BUS_TYPE          (ESP_PANEL_BUS_TYPE_I2C)

#if ESP_PANEL_BOARD_TOUCH_BUS_TYPE == ESP_PANEL_BUS_TYPE_I2C
    // La libreria non deve saltare l'init del bus: lo inizializza lei.
    #define ESP_PANEL_BOARD_TOUCH_BUS_SKIP_INIT_HOST        (0)

    #define ESP_PANEL_BOARD_TOUCH_I2C_HOST_ID               (0)
    // Bus I2C dedicato al touch ("RTC I2C" dallo schematico).
    #define ESP_PANEL_BOARD_TOUCH_I2C_CLK_HZ                (400 * 1000)
    // Pull-up esterni gia' presenti sul PCB (R68/R71 sulla rete RTC_CLK/DAT).
    #define ESP_PANEL_BOARD_TOUCH_I2C_SCL_PULLUP            (0)
    #define ESP_PANEL_BOARD_TOUCH_I2C_SDA_PULLUP            (0)
    // Pin RTC_CLK (SCL) = GPIO8, RTC_DAT (SDA) = GPIO7 (riferimento ESPHome
    // "JC1060P470" verificato sullo schematico Guition V1.0).
    #define ESP_PANEL_BOARD_TOUCH_I2C_IO_SCL                (8)
    #define ESP_PANEL_BOARD_TOUCH_I2C_IO_SDA                (7)
    // GT911: 0 = usa indirizzo di default (0x5D).
    #define ESP_PANEL_BOARD_TOUCH_I2C_ADDRESS               (0)
#endif

#define ESP_PANEL_BOARD_TOUCH_SWAP_XY                       (0)
#define ESP_PANEL_BOARD_TOUCH_MIRROR_X                      (0)
#define ESP_PANEL_BOARD_TOUCH_MIRROR_Y                      (0)

// Il demo ufficiale Guition lascia RST e INT non collegati al controller
// (TP_RST=-1, TP_INT=-1 in `pins_config.h`): il GT911 usa l'indirizzo I2C
// di default (0x5D) e il polling invece dell'interrupt.
#define ESP_PANEL_BOARD_TOUCH_RST_IO                        (-1)
#define ESP_PANEL_BOARD_TOUCH_RST_LEVEL                     (0)
#define ESP_PANEL_BOARD_TOUCH_INT_IO                        (-1)
#define ESP_PANEL_BOARD_TOUCH_INT_LEVEL                     (0)
#endif // ESP_PANEL_BOARD_USE_TOUCH

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////// Backlight ///////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define ESP_PANEL_BOARD_USE_BACKLIGHT           (1)

#if ESP_PANEL_BOARD_USE_BACKLIGHT
#define ESP_PANEL_BOARD_BACKLIGHT_TYPE          (ESP_PANEL_BACKLIGHT_TYPE_PWM_LEDC)

// Il blocco "Blacklighting" dello schematico porta l'ingresso "LCD_PWM" al
// MP3202: GPIO23 sull'ESP32-P4 (abilitazione + regolazione luminosita').
#define ESP_PANEL_BOARD_BACKLIGHT_IO            (23)
#define ESP_PANEL_BOARD_BACKLIGHT_ON_LEVEL      (1)

// Parametri PWM allineati al demo ufficiale Guition: LEDC 20 kHz, 10 bit.
#define ESP_PANEL_BOARD_BACKLIGHT_PWM_FREQ_HZ           (20 * 1000)
#define ESP_PANEL_BOARD_BACKLIGHT_PWM_DUTY_RESOLUTION   (10)

#define ESP_PANEL_BOARD_BACKLIGHT_IDLE_OFF      (0)
#endif // ESP_PANEL_BOARD_USE_BACKLIGHT

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////// IO expander /////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Il Guition JC1060P470C-I-W-Y non richiede IO expander per gestire il
// pannello / touch / backlight: tutti i segnali sono direttamente su GPIO
// dell'ESP32-P4.
#define ESP_PANEL_BOARD_USE_EXPANDER            (0)

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////// Versione file /////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define ESP_PANEL_BOARD_CUSTOM_FILE_VERSION_MAJOR 1
#define ESP_PANEL_BOARD_CUSTOM_FILE_VERSION_MINOR 2
#define ESP_PANEL_BOARD_CUSTOM_FILE_VERSION_PATCH 0

#endif // ESP_PANEL_BOARD_DEFAULT_USE_CUSTOM

// *INDENT-ON*
