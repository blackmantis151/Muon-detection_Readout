#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// FPGA Configuration Pins
#define FPGA_DONE_PIN GPIO_NUM_21    // FPGA DONE pin
#define FPGA_PROG_B_PIN GPIO_NUM_22  // FPGA PROG_B pin
#define FPGA_CCLK_PIN GPIO_NUM_23    // FPGA CCLK pin
#define FPGA_DATA0_PIN GPIO_NUM_25   // FPGA DATA0 pin

// SPI Pin Definitions
#define CS_PIN GPIO_NUM_30   // FPGA CS
#define MOSI_PIN GPIO_NUM_32 // FPGA MOSI
#define MISO_PIN GPIO_NUM_33 // FPGA MISO
#define SCLK_PIN GPIO_NUM_31 // FPGA SCLK

#define BITSTREAM_FILE "/sdcard/spi_led_control.bin"

// SPI Device Handle
spi_device_handle_t spi;

// Function to burn the FPGA bitstream
esp_err_t burn_bitstream_to_fpga(const char *file_path) {
    FILE *file = fopen(file_path, "rb");
    if (!file) {
        printf("Failed to open bitstream file.\n");
        return ESP_FAIL;
    }

    // Assert PROG_B to reset the FPGA
    gpio_set_level(FPGA_PROG_B_PIN, 0);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    gpio_set_level(FPGA_PROG_B_PIN, 1);

    printf("Burning bitstream to FPGA...\n");

    // Send the bitstream to the FPGA
    uint8_t byte;
    while (fread(&byte, 1, 1, file) == 1) {
        for (int i = 0; i < 8; i++) {
            gpio_set_level(FPGA_DATA0_PIN, (byte >> i) & 1); // Send each bit
            gpio_set_level(FPGA_CCLK_PIN, 1);               // Toggle CCLK
            gpio_set_level(FPGA_CCLK_PIN, 0);
        }
    }

    fclose(file);

    // Check if FPGA is configured
    if (gpio_get_level(FPGA_DONE_PIN) == 1) {
        printf("FPGA configured successfully!\n");
        return ESP_OK;
    } else {
        printf("FPGA configuration failed.\n");
        return ESP_FAIL;
    }
}

// Function to send data to FPGA via SPI
void send_data_via_spi(uint8_t data) {
    esp_err_t ret;
    spi_transaction_t transaction = {
        .length = 8, // Send 8 bits (1 byte)
        .tx_buffer = &data,
    };

    gpio_set_level(CS_PIN, 0); // Select FPGA
    ret = spi_device_transmit(spi, &transaction); // Transmit the data
    assert(ret == ESP_OK);
    gpio_set_level(CS_PIN, 1); // Deselect FPGA
}

// Main function
void app_main() {
    esp_err_t ret;

    // Configure FPGA control pins
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << FPGA_PROG_B_PIN) | (1ULL << FPGA_CCLK_PIN) | (1ULL << FPGA_DATA0_PIN) | (1ULL << FPGA_DONE_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(FPGA_PROG_B_PIN, 1); // De-assert PROG_B

    // Burn the FPGA bitstream
    if (burn_bitstream_to_fpga(BITSTREAM_FILE) != ESP_OK) {
        printf("Failed to program FPGA.\n");
        return;
    }

    // Configure SPI pins
    gpio_set_level(CS_PIN, 1); // Deselect FPGA initially
    spi_bus_config_t buscfg = {
        .mosi_io_num = MOSI_PIN,
        .miso_io_num = MISO_PIN,
        .sclk_io_num = SCLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    assert(ret == ESP_OK);

    // Add SPI device
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 1 * 1000 * 1000, // 1 MHz
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 1,
    };
    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &spi);
    assert(ret == ESP_OK);

    printf("SPI initialized successfully.\n");

    // Main Loop
    while (1) {
        char data;
        printf("Enter data to send to FPGA: ");
        scanf(" %c", &data);

        send_data_via_spi((uint8_t)data); // Send the data to FPGA
        printf("Sent to FPGA: %c\n", data);

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
