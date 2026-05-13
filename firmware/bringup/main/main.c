/*
 * ShrikeVault — P0 board bring-up.
 *
 * What this app proves:
 *   1. The firmware runs on the ESP32-S3 (user LED blinks).
 *   2. We can drive the SLG47910 ForgeFPGA's PWR/EN/SS lines and stream a
 *      bitstream into it over SPI2  -> the FPGA's own LED should start blinking
 *      (the embedded test bitstream is Vicharak's led_blink design).
 *   3. A console works over the ESP32-S3 built-in USB Serial/JTAG (the COM port
 *      that already enumerates) so we can poke at things interactively.
 *
 * Console commands:  help | info | fpga_load | fpga_off | io_read | puf
 *
 * This is a DEBUG build — no Secure Boot / Flash Encryption (those are P5).
 */
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_console.h"
#include "driver/ledc.h"
#include <stdlib.h>

#include "shrike_board.h"
#include "fpga_config.h"

static const char *TAG = "bringup";

/* ForgeFPGA test bitstreams embedded via EMBED_FILES in main/CMakeLists.txt. */
extern const uint8_t led_blink_bin_start[]     asm("_binary_led_blink_bin_start");
extern const uint8_t led_blink_bin_end[]       asm("_binary_led_blink_bin_end");
extern const uint8_t breathing_led_bin_start[] asm("_binary_breathing_led_bin_start");
extern const uint8_t breathing_led_bin_end[]   asm("_binary_breathing_led_bin_end");
extern const uint8_t blink_all_bin_start[]     asm("_binary_blink_all_bin_start");
extern const uint8_t blink_all_bin_end[]       asm("_binary_blink_all_bin_end");
extern const uint8_t pll_oscillator_bin_start[] asm("_binary_pll_oscillator_bin_start");
extern const uint8_t pll_oscillator_bin_end[]   asm("_binary_pll_oscillator_bin_end");
extern const uint8_t counter_4bit_bin_start[]  asm("_binary_counter_4bit_bin_start");
extern const uint8_t counter_4bit_bin_end[]    asm("_binary_counter_4bit_bin_end");

typedef struct { const char *name; const uint8_t *start, *end; } embed_t;
static const embed_t s_bitstreams[] = {
    { "led_blink",      led_blink_bin_start,      led_blink_bin_end      },
    { "breathing_led",  breathing_led_bin_start,  breathing_led_bin_end  },
    { "blink_all",      blink_all_bin_start,      blink_all_bin_end      },
    { "pll_oscillator", pll_oscillator_bin_start, pll_oscillator_bin_end },  /* enables on-chip OSC */
    { "counter_4bit",   counter_4bit_bin_start,   counter_4bit_bin_end   },  /* enables on-chip OSC */
};
#define BITSTREAMS_COUNT (sizeof(s_bitstreams) / sizeof(s_bitstreams[0]))

static int load_named(const char *name)
{
    for (size_t i = 0; i < BITSTREAMS_COUNT; i++) {
        if (strcmp(s_bitstreams[i].name, name) == 0) {
            size_t len = (size_t)(s_bitstreams[i].end - s_bitstreams[i].start);
            printf("Streaming %s.bin (%u bytes) into the ForgeFPGA...\n", name, (unsigned)len);
            esp_err_t err = fpga_load_bitstream(s_bitstreams[i].start, len);
            if (err == ESP_OK) { printf("OK — design '%s' should be running now.\n", name); return 0; }
            printf("FAILED: %s\n", esp_err_to_name(err));
            return 1;
        }
    }
    printf("unknown bitstream '%s'; available: ", name);
    for (size_t i = 0; i < BITSTREAMS_COUNT; i++) printf("%s%s", s_bitstreams[i].name, i + 1 < BITSTREAMS_COUNT ? ", " : "\n");
    return 1;
}

/* ---------------------------------------------------------------- LED heartbeat */
/* GPIO is configured in app_main (gpio_config()'s call path is stack-hungry);
 * the task body is deliberately trivial so a small stack is plenty. */
static void led_task(void *arg)
{
    bool on = false;
    for (;;) {
        on = !on;
        gpio_set_level(SHRIKE_PIN_LED, on);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* ---------------------------------------------------------------- console cmds */
static int cmd_info(int argc, char **argv)
{
    esp_chip_info_t ci; esp_chip_info(&ci);
    uint32_t flash_sz = 0; esp_flash_get_size(NULL, &flash_sz);
    printf("ShrikeVault bring-up (P0)\n");
    printf("  chip       : %s, %d core(s), rev %d.%d\n",
           CONFIG_IDF_TARGET, ci.cores, ci.revision / 100, ci.revision % 100);
    printf("  flash      : %lu MB\n", (unsigned long)(flash_sz / (1024 * 1024)));
    printf("  free heap  : %lu bytes\n", (unsigned long)esp_get_free_heap_size());
    printf("  FPGA cfg   : SPI2  PWR=GPIO%d EN=GPIO%d SS=GPIO%d SCK=GPIO%d MOSI=GPIO%d MISO=GPIO%d\n",
           SHRIKE_PIN_FPGA_PWR, SHRIKE_PIN_FPGA_EN, SHRIKE_PIN_FPGA_SS,
           SHRIKE_PIN_FPGA_SCK, SHRIKE_PIN_FPGA_MOSI, SHRIKE_PIN_FPGA_MISO);
    printf("  FPGA IO bus: the 4 SPI pins double as a 4-bit IO bus post-config\n");
    printf("  user LED   : ESP32 GPIO%d  |  FPGA's own LED: FPGA pin 16 (not MCU-readable)\n", SHRIKE_PIN_LED);
    printf("  bitstream  : embedded led_blink.bin, %u bytes (expect %d)\n",
           (unsigned)(led_blink_bin_end - led_blink_bin_start), SHRIKE_FPGA_BITSTREAM_BYTES);
    return 0;
}

static int cmd_fpga_load(int argc, char **argv)
{
    const char *name = (argc > 1) ? argv[1] : "led_blink";
    return load_named(name);
}

static int cmd_fpga_list(int argc, char **argv)
{
    printf("Embedded bitstreams:\n");
    for (size_t i = 0; i < BITSTREAMS_COUNT; i++) {
        printf("  %-13s  %u bytes\n", s_bitstreams[i].name,
               (unsigned)(s_bitstreams[i].end - s_bitstreams[i].start));
    }
    printf("Usage: fpga_load <name>   (defaults to led_blink)\n");
    return 0;
}

static int cmd_fpga_off(int argc, char **argv) { fpga_power_down(); printf("FPGA powered down.\n"); return 0; }

static int cmd_io_read(int argc, char **argv)
{
    uint8_t v = fpga_read_io_lines();
    printf("FPGA IO bus (4 SPI pins read as inputs): 0x%x  [", v);
    for (int i = SHRIKE_FPGA_IO_COUNT - 1; i >= 0; i--) printf("%s=%d ", SHRIKE_FPGA_IO_NAME[i], (v >> i) & 1);
    printf("]\n");
    return 0;
}

/* ---------- drive a clock on a GPIO (to feed the FPGA's clk pin) ---------- */
static bool s_clk_running = false;
static gpio_num_t s_clk_gpio = GPIO_NUM_NC;

static int cmd_clk_drive(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: clk_drive <gpio> [hz]\n");
        printf("       drives a 50%% square wave from the ESP32 LEDC on <gpio>.\n");
        printf("       try the 4 SPI pins post-config: SS=10  MOSI=11  SCK=12  MISO=13\n");
        printf("       example:  clk_drive 12 25000000\n");
        return 1;
    }
    int gpio = atoi(argv[1]);
    int hz   = (argc >= 3) ? atoi(argv[2]) : 25000000;   /* 25 MHz default; LED toggles ~every 2 s */
    if (gpio < 0 || gpio > 48) { printf("bad gpio %d\n", gpio); return 1; }

    /* If we were already driving a clock, stop it first so we can reuse the timer. */
    if (s_clk_running) ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);

    ledc_timer_config_t t = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_1_BIT,
        .timer_num       = LEDC_TIMER_0,
        .freq_hz         = hz,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&t);
    if (err != ESP_OK) { printf("ledc_timer_config(%d Hz) failed: %s — try lower freq\n", hz, esp_err_to_name(err)); return 1; }

    ledc_channel_config_t c = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .timer_sel  = LEDC_TIMER_0,
        .gpio_num   = gpio,
        .duty       = 1,                 /* 1/2 = 50 % */
        .hpoint     = 0,
    };
    err = ledc_channel_config(&c);
    if (err != ESP_OK) { printf("ledc_channel_config failed: %s\n", esp_err_to_name(err)); return 1; }

    s_clk_running = true;
    s_clk_gpio = (gpio_num_t)gpio;
    printf("driving %d Hz on GPIO%d (50%% duty). use 'clk_stop' to release.\n", hz, gpio);
    printf("if this is the FPGA's clk pin and led_blink is loaded, LED should toggle every ~%d ms\n",
           (int)(50000000LL * 1000 / hz));   /* design counts to 50M then toggles */
    return 0;
}

static int cmd_clk_stop(int argc, char **argv)
{
    if (!s_clk_running) { printf("no clock running.\n"); return 0; }
    ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    /* Park the pin: drive low so it's a definite known state. */
    gpio_reset_pin(s_clk_gpio);
    gpio_set_direction(s_clk_gpio, GPIO_MODE_OUTPUT);
    gpio_set_level(s_clk_gpio, 0);
    s_clk_running = false;
    printf("clock stopped; GPIO%d parked low.\n", s_clk_gpio);
    return 0;
}

/* Sweep: try each SPI pin in turn for a few seconds; user watches the LED. */
static int cmd_clk_sweep(int argc, char **argv)
{
    const int hz = (argc >= 2) ? atoi(argv[1]) : 25000000;
    const int dwell_ms = (argc >= 3) ? atoi(argv[2]) : 4000;
    const gpio_num_t candidates[] = {
        SHRIKE_PIN_FPGA_SCK,    /* GPIO12 — FPGA pin 3, most likely candidate */
        SHRIKE_PIN_FPGA_MOSI,   /* GPIO11 — FPGA pin 5 */
        SHRIKE_PIN_FPGA_MISO,   /* GPIO13 — FPGA pin 6 */
        SHRIKE_PIN_FPGA_SS,     /* GPIO10 — FPGA pin 4 */
    };
    const char *names[] = { "SCK(GPIO12,FPGA-pin-3)", "MOSI(GPIO11,FPGA-pin-5)",
                            "MISO(GPIO13,FPGA-pin-6)", "SS(GPIO10,FPGA-pin-4)" };
    printf("Sweeping clock @ %d Hz across the 4 SPI pins, %d ms dwell each.\n", hz, dwell_ms);
    printf("Watch the FPGA LED — when it starts toggling, that pin is the clk input.\n");
    for (size_t i = 0; i < sizeof(candidates)/sizeof(candidates[0]); i++) {
        printf("[%zu/4] driving %s ...\n", i + 1, names[i]);
        char gbuf[8], hbuf[16];
        snprintf(gbuf, sizeof(gbuf), "%d", candidates[i]);
        snprintf(hbuf, sizeof(hbuf), "%d", hz);
        char *argv2[] = { "clk_drive", gbuf, hbuf };
        cmd_clk_drive(3, argv2);
        vTaskDelay(pdMS_TO_TICKS(dwell_ms));
    }
    cmd_clk_stop(0, NULL);
    printf("sweep done.\n");
    return 0;
}

static int cmd_puf(int argc, char **argv)
{
    printf("PUF: not implemented yet (P1).\n");
    printf("Plan: RO-PUF bitstream on the ForgeFPGA exposing a challenge/response\n");
    printf("      register slave over GPIO35..40; this command will sweep RO pairs\n");
    printf("      and dump edge counts for the host-side characterisation script.\n");
    printf("      See docs/roadmap.md (P1) and docs/architecture.md.\n");
    return 0;
}

static void register_commands(void)
{
    const esp_console_cmd_t cmds[] = {
        { .command = "info",      .help = "Board / build info",                                         .func = &cmd_info },
        { .command = "fpga_load", .help = "Stream a bitstream into the FPGA: fpga_load [name]",         .func = &cmd_fpga_load },
        { .command = "fpga_list", .help = "List embedded bitstreams",                                   .func = &cmd_fpga_list },
        { .command = "fpga_off",  .help = "Power down the FPGA",                                        .func = &cmd_fpga_off },
        { .command = "io_read",   .help = "Read the 4 SPI/IO-bus pins as inputs",                       .func = &cmd_io_read },
        { .command = "clk_drive", .help = "Drive a clock from ESP32 LEDC: clk_drive <gpio> [hz]",       .func = &cmd_clk_drive },
        { .command = "clk_sweep", .help = "Sweep a clock across SS/MOSI/SCK/MISO: clk_sweep [hz] [ms]", .func = &cmd_clk_sweep },
        { .command = "clk_stop",  .help = "Stop the LEDC clock and park the pin low",                   .func = &cmd_clk_stop },
        { .command = "puf",       .help = "(stub) PUF challenge/response — coming in P1",          .func = &cmd_puf },
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
}

/* ---------------------------------------------------------------- app_main */
void app_main(void)
{
    ESP_LOGI(TAG, "ShrikeVault bring-up starting (DEBUG build — no Secure Boot/Flash Enc)");

    ESP_ERROR_CHECK(fpga_config_init());

    /* ESP32 user-LED heartbeat. (Configure GPIO here, not in the task — see led_task.) */
    gpio_config_t led_io = {
        .pin_bit_mask = 1ULL << SHRIKE_PIN_LED,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&led_io));
    xTaskCreate(led_task, "led", 2048, NULL, 3, NULL);

    /* Auto-load the test bitstream once at boot so a fresh flash immediately
     * shows both LEDs blinking if everything is wired right. */
    {
        size_t len = (size_t)(led_blink_bin_end - led_blink_bin_start);
        ESP_LOGI(TAG, "auto-loading embedded led_blink.bin (%u bytes) into the FPGA", (unsigned)len);
        esp_err_t err = fpga_load_bitstream(led_blink_bin_start, len);
        ESP_LOGI(TAG, "FPGA load: %s", esp_err_to_name(err));
    }

    /* Console over the built-in USB Serial/JTAG (the device that shows up as a COM port). */
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "shrikevault> ";
    repl_cfg.max_cmdline_length = 256;
    esp_console_dev_usb_serial_jtag_config_t hw = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&hw, &repl_cfg, &repl));
    esp_console_register_help_command();
    register_commands();
    ESP_ERROR_CHECK(esp_console_start_repl(repl));

    ESP_LOGI(TAG, "ready — type 'help' on the console (try 'info', 'fpga_load')");
}
