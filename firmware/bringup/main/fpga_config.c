/*
 * fpga_config.c — SLG47910 ForgeFPGA SRAM-config driver. See fpga_config.h
 * and docs/hardware-notes.md for the protocol.
 */
#include "fpga_config.h"
#include "shrike_board.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_check.h"     /* ESP_RETURN_ON_FALSE */
#include "esp_rom_sys.h"   /* esp_rom_delay_us */
#include "esp_heap_caps.h" /* heap_caps_malloc(MALLOC_CAP_DMA) */

static const char *TAG = "fpga_cfg";

static spi_device_handle_t s_fpga_spi = NULL;
static bool s_inited = false;

/* SS is a plain GPIO we drive by hand (the cfg handshake needs a CS *pulse*
 * separate from the data stream, so we don't let the SPI driver own it). */
static inline void ss_set(int level) { gpio_set_level(SHRIKE_PIN_FPGA_SS, level); }

static void delay_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

esp_err_t fpga_config_init(void)
{
    if (s_inited) return ESP_OK;

    /* Control GPIOs: PWR, EN, SS — start with the FPGA powered down. */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << SHRIKE_PIN_FPGA_PWR) |
                        (1ULL << SHRIKE_PIN_FPGA_EN)  |
                        (1ULL << SHRIKE_PIN_FPGA_SS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_FALSE(gpio_config(&io) == ESP_OK, ESP_FAIL, TAG, "gpio_config failed");
    gpio_set_level(SHRIKE_PIN_FPGA_SS, 1);
    gpio_set_level(SHRIKE_PIN_FPGA_EN, 0);
    gpio_set_level(SHRIKE_PIN_FPGA_PWR, 0);

    /* SPI2 bus: MOSI + SCK only (config is write-only); generous DMA limit so
     * the ~46 KB bitstream can go in a few chunks. */
    spi_bus_config_t bus = {
        .mosi_io_num = SHRIKE_PIN_FPGA_MOSI,
        .miso_io_num = SHRIKE_PIN_FPGA_MISO,   /* mapped but unused for cfg */
        .sclk_io_num = SHRIKE_PIN_FPGA_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 48 * 1024,
    };
    ESP_RETURN_ON_FALSE(spi_bus_initialize(SHRIKE_FPGA_SPI_HOST, &bus, SPI_DMA_CH_AUTO) == ESP_OK,
                        ESP_FAIL, TAG, "spi_bus_initialize failed");

    spi_device_interface_config_t dev = {
        .clock_speed_hz = SHRIKE_FPGA_SPI_HZ,
        .mode = 0,                       /* CPOL=0, CPHA=0 */
        .spics_io_num = -1,              /* we drive SS ourselves */
        .queue_size = 4,
        .flags = 0,                      /* MSB-first (default) */
    };
    ESP_RETURN_ON_FALSE(spi_bus_add_device(SHRIKE_FPGA_SPI_HOST, &dev, &s_fpga_spi) == ESP_OK,
                        ESP_FAIL, TAG, "spi_bus_add_device failed");

    s_inited = true;
    ESP_LOGI(TAG, "init OK  (SPI2 @ %d Hz; PWR=GPIO%d EN=GPIO%d SS=GPIO%d SCK=GPIO%d MOSI=GPIO%d)",
             SHRIKE_FPGA_SPI_HZ, SHRIKE_PIN_FPGA_PWR, SHRIKE_PIN_FPGA_EN,
             SHRIKE_PIN_FPGA_SS, SHRIKE_PIN_FPGA_SCK, SHRIKE_PIN_FPGA_MOSI);
    return ESP_OK;
}

void fpga_power_down(void)
{
    ss_set(1);
    gpio_set_level(SHRIKE_PIN_FPGA_EN, 0);
    gpio_set_level(SHRIKE_PIN_FPGA_PWR, 0);
    ESP_LOGI(TAG, "FPGA powered down");
}

esp_err_t fpga_load_bitstream(const uint8_t *bitstream, size_t len)
{
    if (!s_inited) { ESP_LOGE(TAG, "not initialised"); return ESP_ERR_INVALID_STATE; }
    if (!bitstream || len == 0) return ESP_ERR_INVALID_ARG;
    if (len != SHRIKE_FPGA_BITSTREAM_BYTES) {
        ESP_LOGW(TAG, "bitstream is %u bytes, expected %d for SLG47910 — streaming anyway",
                 (unsigned)len, SHRIKE_FPGA_BITSTREAM_BYTES);
    }

    /* --- power-cycle & arm (mirrors Vicharak's shrike.flash() exactly) --- */
    ss_set(0);                                  /* SS low first */
    gpio_set_level(SHRIKE_PIN_FPGA_EN, 0);
    gpio_set_level(SHRIKE_PIN_FPGA_PWR, 0);
    delay_ms(100);
    gpio_set_level(SHRIKE_PIN_FPGA_EN, 1);
    gpio_set_level(SHRIKE_PIN_FPGA_PWR, 1);
    delay_ms(100);
    ss_set(1);
    esp_rom_delay_us(2000);
    ss_set(0);                                  /* begin configuration */

    /* --- stream the bitstream, CS held low throughout, in DMA-friendly chunks ---
     * The source may be flash-mapped (EMBED_FILES lands in .rodata in flash),
     * and SPI DMA can only read from internal DMA-capable RAM — so bounce each
     * chunk through a small DMA buffer. */
    esp_err_t err = ESP_OK;
    const size_t CHUNK = 4096;
    uint8_t *dma = heap_caps_malloc(CHUNK, MALLOC_CAP_DMA);
    if (!dma) { ESP_LOGE(TAG, "no DMA memory for bounce buffer"); ss_set(1); return ESP_ERR_NO_MEM; }

    for (size_t off = 0; off < len && err == ESP_OK; off += CHUNK) {
        size_t n = (len - off < CHUNK) ? (len - off) : CHUNK;
        memcpy(dma, bitstream + off, n);
        spi_transaction_t t = {
            .length = n * 8,            /* in bits */
            .tx_buffer = dma,
        };
        err = spi_device_polling_transmit(s_fpga_spi, &t);
        if (err != ESP_OK) ESP_LOGE(TAG, "SPI tx failed at offset %u: %s", (unsigned)off, esp_err_to_name(err));
    }
    heap_caps_free(dma);

    esp_rom_delay_us(200);
    delay_ms(100);
    ss_set(1);                /* configuration complete */

    if (err == ESP_OK) ESP_LOGI(TAG, "streamed %u bytes; FPGA should now be running its design", (unsigned)len);
    return err;
}

uint8_t fpga_read_io_lines(void)
{
    uint8_t v = 0;
    for (int i = 0; i < SHRIKE_FPGA_IO_COUNT; i++) {
        gpio_set_direction(SHRIKE_PIN_FPGA_IO[i], GPIO_MODE_INPUT);
        if (gpio_get_level(SHRIKE_PIN_FPGA_IO[i])) v |= (uint8_t)(1u << i);
    }
    return v;
}
