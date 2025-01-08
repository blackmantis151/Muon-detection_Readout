#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"

// FPGA Configuration Pins
#define FPGA_PROG_B_PIN GPIO_NUM_22  // FPGA PROG_B pin
#define FPGA_CCLK_PIN GPIO_NUM_23    // FPGA Clock pin
#define FPGA_DATA0_PIN GPIO_NUM_25   // FPGA Data pin
#define FPGA_DONE_PIN GPIO_NUM_21    // FPGA DONE pin

// SD Card Configuration
#define MOUNT_POINT "/sdcard"
#define BITSTREAM_FILE "/sdcard/bitstream.bin"

// Function to program FPGA with the bitstream
esp_err_t program_fpga(const char *file_path) {
    FILE *file = fopen(file_path, "rb");
    if (!file) {
        printf("Failed to open bitstream file: %s\n", file_path);
        return ESP_FAIL;
    }

    // Assert PROG_B to reset the FPGA
    gpio_set_level(FPGA_PROG_B_PIN, 0);
    vTaskDelay(10 / portTICK_PERIOD_MS); // Wait 10 ms
    gpio_set_level(FPGA_PROG_B_PIN, 1);

    printf("Starting FPGA configuration...\n");

    // Send bitstream data
    uint8_t byte;
    while (fread(&byte, 1, 1, file) == 1) {
        for (int i = 0; i < 8; i++) {
            gpio_set_level(FPGA_DATA0_PIN, (byte >> i) & 1); // Send each bit
            gpio_set_level(FPGA_CCLK_PIN, 1);               // Toggle clock
            gpio_set_level(FPGA_CCLK_PIN, 0);
        }
    }

    fclose(file);

    // Check the FPGA DONE pin
    if (gpio_get_level(FPGA_DONE_PIN) == 1) {
        printf("FPGA configuration successful!\n");
        return ESP_OK;
    } else {
        printf("FPGA configuration failed. DONE pin not high.\n");
        return ESP_FAIL;
    }
}

// Main application
void app_main() {
    esp_err_t ret;

    // Configure FPGA GPIO pins
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << FPGA_PROG_B_PIN) |
                        (1ULL << FPGA_CCLK_PIN) |
                        (1ULL << FPGA_DATA0_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // Configure DONE pin as input
    gpio_set_direction(FPGA_DONE_PIN, GPIO_MODE_INPUT);

    // Mount the SD card
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
    };
    sdmmc_card_t *card;
    ret = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        printf("Failed to mount SD card.\n");
        return;
    }
    printf("SD card mounted successfully.\n");

    // Program the FPGA
    if (program_fpga(BITSTREAM_FILE) != ESP_OK) {
        printf("FPGA programming failed.\n");
    } else {
        printf("FPGA programming completed successfully.\n");
    }

    // Unmount the SD card
    esp_vfs_fat_sdmmc_unmount();
    printf("SD card unmounted.\n");
}
