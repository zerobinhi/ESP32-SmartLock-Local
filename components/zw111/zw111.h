#ifndef ZW111_H_
#define ZW111_H_

#include <driver/uart.h>
#include <driver/gpio.h>
#include "app_config.h"
#include "buzzer.h"

#define EX_UART_NUM UART_NUM_2 // UART port used by fingerprint module

#define CHECKSUM_LEN 2         // Checksum length (bytes, fixed to 2)
#define CHECKSUM_START_INDEX 6 // Checksum calculation start index (packet identifier position)

#define PACKET_CMD 0x01       // Command packet (host sends instruction)
#define PACKET_DATA_MORE 0x02 // Data packet (with subsequent packets)
#define PACKET_DATA_LAST 0x08 // Last data packet (no subsequent packets)
#define PACKET_RESPONSE 0x07  // Response packet (module returns result)

#define CMD_AUTO_ENROLL 0x31      // Auto-enroll fingerprint command
#define CMD_AUTO_IDENTIFY 0x32    // Auto-identify fingerprint command
#define CMD_CONTROL_BLN 0x3C      // Backlight (LED) control command
#define CMD_DELET_CHAR 0x0C       // Delete specified fingerprint command
#define CMD_EMPTY 0x0D            // Clear all fingerprints command
#define CMD_CANCEL 0x30           // Cancel current operation command
#define CMD_READ_INDEX_TABLE 0x1F // Read fingerprint index table command
#define CMD_SLEEP 0x33            // Module sleep command

#define BLN_BREATH 1   // Normal breathing light mode
#define BLN_FLASH 2    // Flashing light mode
#define BLN_ON 3       // Always on mode
#define BLN_OFF 4      // Always off mode
#define BLN_FADE_IN 5  // Fade-in mode
#define BLN_FADE_OUT 6 // Fade-out mode
#define BLN_COLORFUL 7 // Colorful cycle mode

// LED color definition (bit0-Blue, bit1-Green, bit2-Red)
#define LED_OFF 0x00   // All lights off
#define LED_BLUE 0x01  // Blue light only
#define LED_GREEN 0x02 // Green light only
#define LED_RED 0x04   // Red light only
#define LED_BG 0x03    // Blue + Green lights
#define LED_BR 0x05    // Blue + Red lights
#define LED_GR 0x06    // Green + Red lights
#define LED_ALL 0x07   // Red + Green + Blue (all lights on)

// ========================== Data Structure Definition ==========================
struct fingerprint_device
{
    /**
     * 0X00 Just powered on state
     * 0X01 Read index table state
     * 0X02 Enroll fingerprint state
     * 0X03 Delete fingerprint state
     * 0X04 Verify fingerprint state
     * 0X0A Cancel command state
     * 0X0B Prepare to power off state
     */
    uint8_t state;

    /**
     * false Power-off state
     * true Power-on state
     */
    bool power;

    // Device address (4 bytes), default address 0xFFFFFFFF, modifiable
    uint8_t deviceAddress[4];

    // Enrolled fingerprint ID array, maximum 100 entries (0-99), unused positions are 0xFF
    uint8_t fingerIDArray[100];

    // Current number of valid fingerprints
    uint8_t fingerNumber;
};

extern bool g_ready_add_fingerprint;                                  // Flag for preparing to add fingerprint
extern bool g_cancel_add_fingerprint;                                 // Flag for canceling fingerprint addition
extern bool g_ready_delete_fingerprint;                               // Flag for preparing to delete fingerprint
extern bool g_ready_delete_all_fingerprint;                           // Flag for preparing to delete all fingerprints
extern uint8_t g_deleteFingerprintID;                                 // Fingerprint ID to be deleted
extern void send_fingerprint_list();                                  // Send current fingerprint list to front-end
extern void send_operation_result(const char *message, bool success); // Send operation result to front-end
extern bool g_gpio_isr_service_installed;                             // Whether GPIO interrupt service is installed
extern QueueHandle_t fingerprint_queue;                               // Message queue from fingerprint module to buzzer

void fingerprint_task(void *pvParameters);
void uart_task(void *pvParameters);
esp_err_t fingerprint_initialization();
void turn_on_fingerprint();
void prepare_turn_off_fingerprint();
void cancel_current_operation_and_execute_command();

#endif
