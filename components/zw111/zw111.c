#include "zw111.h"

const uint8_t FRAME_HEADER[2] = {0xEF, 0x01}; // Fixed frame header value for fingerprint module

struct fingerprint_device zw111 = {0}; // Fingerprint module structure instance

SemaphoreHandle_t fingerprint_semaphore = NULL; // Semaphore for fingerprint module, only used to activate the module after touch detection

static QueueHandle_t uart2_queue; // UART2 event queue

static const char *TAG = "ZW111";

/**
 * @brief Calculate the checksum of the data frame
 * @param receive_data Data frame buffer
 * @param data_length Total length of the data frame
 * @return uint16_t Calculated 16-bit checksum (high byte first), returns 0 if parameters are invalid
 */
static uint16_t calculate_checksum(const uint8_t *receive_data, uint16_t data_length)
{
    uint16_t checksum = 0;
    uint8_t checksumEndIndex = data_length - CHECKSUM_LEN - 1; // Index of the byte before checksum field
    // Accumulation range for checksum: from CHECKSUM_START_INDEX to checksumEndIndex
    for (uint8_t i = CHECKSUM_START_INDEX; i <= checksumEndIndex; i++)
    {
        checksum += receive_data[i];
    }
    return (checksum & 0xFFFF);
}

/**
 * @brief Verify the validity of received data from fingerprint module
 * @param receive_data Received data packet buffer
 * @param data_length Actual number of received bytes
 * @return esp_err_t Verification result: ESP_OK = valid data, ESP_FAIL = invalid data
 */
static esp_err_t verify_received_data(const uint8_t *receive_data, uint16_t data_length)
{
    // Basic validity check
    if (receive_data == NULL || data_length < 12)
    {
        ESP_LOGE(TAG, "Verification failed: Data is null or length insufficient (min 9 bytes required, actual %u)", data_length);
        return ESP_FAIL;
    }
    // Verify data length
    uint16_t expectedDataLen = (receive_data[7] << 8) | receive_data[8]; // Data field length
    if (expectedDataLen + 9 != data_length)
    {
        ESP_LOGE(TAG, "Verification failed: Length mismatch (expected total length %u, actual %u)", 9 + expectedDataLen, data_length);
        return ESP_FAIL;
    }
    // Verify frame header
    if (receive_data[0] != FRAME_HEADER[0] || receive_data[1] != FRAME_HEADER[1])
    {
        ESP_LOGE(TAG, "Verification failed: Frame header mismatch (expected %02X%02X, actual %02X%02X)", FRAME_HEADER[0], FRAME_HEADER[1], receive_data[0], receive_data[1]);
        return ESP_FAIL;
    }
    // Verify device address
    for (int i = 2; i < 6; i++)
    {
        if (receive_data[i] != zw111.deviceAddress[i - 2])
        {
            ESP_LOGE(TAG, "Verification failed: Device address mismatch (expected %02X%02X%02X%02X, actual %02X%02X%02X%02X)",
                     zw111.deviceAddress[0], zw111.deviceAddress[1], zw111.deviceAddress[2], zw111.deviceAddress[3],
                     receive_data[2], receive_data[3], receive_data[4], receive_data[5]);
            return ESP_FAIL;
        }
    }
    // Verify packet identifier
    if (receive_data[6] != PACKET_RESPONSE)
    {
        ESP_LOGE(TAG, "Verification failed: Incorrect packet identifier (expected response packet %02X, actual %02X)", PACKET_RESPONSE, receive_data[6]);
        return ESP_FAIL;
    }
    // Verify checksum
    uint16_t receivedChecksum = (receive_data[data_length - 2] << 8) | receive_data[data_length - 1];
    if (calculate_checksum(receive_data, data_length) != receivedChecksum)
    {
        ESP_LOGE(TAG, "Verification failed: Checksum mismatch (expected 0x%04X, actual 0x%04X)", calculate_checksum(receive_data, data_length), receivedChecksum);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Verification succeeded: Data is valid");
    return ESP_OK;
}

/**
 * @brief Auto-enrollment function for fingerprint module
 * @param ID Fingerprint ID (0-99, returns failure if out of range)
 * @param enrollTimes Enrollment times (2-255, returns failure if out of range)
 * @param ledControl Image capture backlight control: false = always on; true = off after successful capture
 * @param preprocess Image capture preprocessing control: false = no preprocessing; true = enable preprocessing
 * @param returnStatus Enrollment status return control: false = return status; true = no status return
 * @param allowOverwrite ID overwrite control: false = not allowed; true = allowed
 * @param allowDuplicate Duplicate enrollment control: false = allowed; true = prohibited
 * @param requireRemove Finger removal requirement: false = need to remove; true = no need to remove
 * @return esp_err_t Operation result: ESP_OK = command sent successfully, ESP_FAIL = command sent failed
 */
static esp_err_t auto_enroll(uint16_t ID, uint8_t enrollTimes, bool ledControl, bool preprocess, bool returnStatus, bool allowOverwrite, bool allowDuplicate, bool requireRemove)
{
    // Check ID validity
    if (ID >= 100)
    {
        ESP_LOGE(TAG, "Enrollment failed: ID out of range (0-99 required, current %u)", ID);
        return ESP_FAIL;
    }
    // Check enrollment times validity
    if (enrollTimes < 2)
    {
        ESP_LOGE(TAG, "Enrollment failed: Enrollment times out of range (2-255 required, current %u)", enrollTimes);
        return ESP_FAIL;
    }
    // Assemble control parameters
    uint16_t param = 0;
    param |= (ledControl ? 1 << 0 : 0);     // bit0: Backlight control
    param |= (preprocess ? 1 << 1 : 0);     // bit1: Preprocessing control
    param |= (returnStatus ? 1 << 2 : 0);   // bit2: Status return control
    param |= (allowOverwrite ? 1 << 3 : 0); // bit3: ID overwrite control
    param |= (allowDuplicate ? 1 << 4 : 0); // bit4: Duplicate enrollment control
    param |= (requireRemove ? 1 << 5 : 0);  // bit5: Finger removal control
    // Construct data frame
    uint8_t frame[17] = {
        FRAME_HEADER[0], FRAME_HEADER[1],                                                               // Frame header (2 bytes)
        zw111.deviceAddress[0], zw111.deviceAddress[1], zw111.deviceAddress[2], zw111.deviceAddress[3], // Device address (4 bytes)
        PACKET_CMD,                                                                                     // Packet identifier (1 byte)
        0x00, 0x08,                                                                                     // Data length (2 bytes, fixed 8 bytes)
        CMD_AUTO_ENROLL,                                                                                // Command code (1 byte)
        (uint8_t)(ID >> 8), (uint8_t)ID,                                                                // Fingerprint ID (2 bytes, high byte first)
        enrollTimes,                                                                                    // Enrollment times (1 byte)
        (uint8_t)(param >> 8), (uint8_t)param,                                                          // Control parameters (2 bytes, high byte first)
        0x00, 0x00                                                                                      // Checksum (2 bytes, to be calculated)
    };
    // Calculate and fill checksum
    uint16_t checksum = calculate_checksum(frame, sizeof(frame));
    frame[15] = (uint8_t)(checksum >> 8);   // High byte of checksum
    frame[16] = (uint8_t)(checksum & 0xFF); // Low byte of checksum

    // Send command via UART
    int len = uart_write_bytes(EX_UART_NUM, (const char *)frame, sizeof(frame));
    if (len == sizeof(frame))
    {
        // Send succeeded
        ESP_LOGI(TAG, "Auto-enrollment command sent successfully");
        return ESP_OK;
    }
    else
    {
        // Send failed
        ESP_LOGE(TAG, "Sending failed, actual bytes sent: %d", len);
        return ESP_FAIL;
    }
}

/**
 * @brief Auto-identification function for fingerprint module
 * @param ID Fingerprint ID: specific value (0-99) = verify specified ID; 0xFFFF = verify all enrolled fingerprints
 * @param scoreLevel Matching score level (1-5, higher level = stricter matching, default recommended 2)
 * @param ledControl Image capture backlight control: false = always on; true = off after successful capture
 * @param preprocess Image capture preprocessing control: false = no preprocessing; true = enable preprocessing
 * @param returnStatus Identification status return control: false = return status; true = no status return
 * @return esp_err_t Operation result: ESP_OK = command sent successfully, ESP_FAIL = invalid parameters or command sent failed
 */
static esp_err_t auto_identify(uint16_t ID, uint8_t scoreLevel, bool ledControl, bool preprocess, bool returnStatus)
{
    if (scoreLevel < 1 || scoreLevel > 5)
    {
        ESP_LOGE(TAG, "Auto-identification failed: Invalid score level (1-5 required, current %u)", scoreLevel);
        return ESP_FAIL;
    }
    // Assemble control parameters
    uint16_t param = 0;
    param |= (ledControl ? 1 << 0 : 0);   // bit0: Backlight control
    param |= (preprocess ? 1 << 1 : 0);   // bit1: Preprocessing control
    param |= (returnStatus ? 1 << 2 : 0); // bit2: Status return control
    // Construct data frame
    uint8_t frame[17] = {
        FRAME_HEADER[0], FRAME_HEADER[1],                                                               // Frame header (2 bytes)
        zw111.deviceAddress[0], zw111.deviceAddress[1], zw111.deviceAddress[2], zw111.deviceAddress[3], // Device address (4 bytes)
        PACKET_CMD,                                                                                     // Packet identifier (1 byte)
        0x00, 0x08,                                                                                     // Data length (2 bytes, fixed 8 bytes)
        CMD_AUTO_IDENTIFY,                                                                              // Command code (1 byte)
        scoreLevel,                                                                                     // Score level (1 byte)
        (uint8_t)(ID >> 8), (uint8_t)ID,                                                                // Fingerprint ID (2 bytes, high byte first)
        (uint8_t)(param >> 8), (uint8_t)param,                                                          // Control parameters (2 bytes, high byte first)
        0x00, 0x00                                                                                      // Checksum (2 bytes, to be calculated)
    };
    // Calculate and fill checksum
    uint16_t checksum = calculate_checksum(frame, sizeof(frame));
    frame[15] = (uint8_t)(checksum >> 8);   // High byte of checksum
    frame[16] = (uint8_t)(checksum & 0xFF); // Low byte of checksum

    // Send command via UART
    int len = uart_write_bytes(EX_UART_NUM, (const char *)frame, sizeof(frame));
    if (len == sizeof(frame))
    {
        // Send succeeded
        ESP_LOGI(TAG, "Auto-identification command sent successfully");
        return ESP_OK;
    }
    else
    {
        // Send failed
        ESP_LOGE(TAG, "Sending failed, actual bytes sent: %d", len);
        return ESP_FAIL;
    }
}

/**
 * @brief LED control function for fingerprint module (supports breathing, flashing, on/off modes)
 * @param functionCode Function code (1-6, refer to BLN_xxx macros, e.g., BLN_BREATH = breathing light)
 * @param startColor Start color (bit0-Blue, bit1-Green, bit2-Red, refer to LED_xxx macros)
 * @param endColor End color (only valid for function code 1 - breathing light, ignored for other modes)
 * @param cycleTimes Cycle times (only valid for function code 1-breathing/2-flashing, 0 = infinite cycle)
 * @return esp_err_t Operation result: ESP_OK = command sent successfully, ESP_FAIL = invalid parameters or command sent failed
 */
static esp_err_t control_led(uint8_t functionCode, uint8_t startColor, uint8_t endColor, uint8_t cycleTimes)
{
    // Parameter validity check
    if (functionCode < BLN_BREATH || functionCode > BLN_FADE_OUT)
    {
        ESP_LOGE(TAG, "LED control failed: Invalid function code (1-6 required, current %u)", functionCode);
        return ESP_FAIL;
    }
    // Filter invalid bits of color parameters
    if ((startColor & 0xF8) != 0)
    {
        ESP_LOGE(TAG, "LED control warning: Only lower 3 bits of start color are valid, filtered to 0x%02X\n", startColor & 0x07);
        startColor &= 0x07;
    }
    if ((endColor & 0xF8) != 0)
    {
        ESP_LOGE(TAG, "LED control warning: Only lower 3 bits of end color are valid, filtered to 0x%02X\n", endColor & 0x07);
        endColor &= 0x07;
    }
    // Construct data frame
    uint8_t frame[16] = {
        FRAME_HEADER[0], FRAME_HEADER[1],                                                               // Frame header (2 bytes)
        zw111.deviceAddress[0], zw111.deviceAddress[1], zw111.deviceAddress[2], zw111.deviceAddress[3], // Device address (4 bytes)
        PACKET_CMD,                                                                                     // Packet identifier (1 byte)
        0x00, 0x07,                                                                                     // Data length (2 bytes, fixed 7 bytes)
        CMD_CONTROL_BLN,                                                                                // Command code (1 byte)
        functionCode,                                                                                   // Function code (1 byte)
        startColor,                                                                                     // Start color (1 byte)
        endColor,                                                                                       // End color (1 byte)
        cycleTimes,                                                                                     // Cycle times (1 byte)
        0x00, 0x00                                                                                      // Checksum (2 bytes, to be calculated)
    };
    // Calculate and fill checksum
    uint16_t checksum = calculate_checksum(frame, sizeof(frame));
    frame[14] = (uint8_t)(checksum >> 8);   // High byte of checksum
    frame[15] = (uint8_t)(checksum & 0xFF); // Low byte of checksum

    // Send command via UART
    int len = uart_write_bytes(EX_UART_NUM, (const char *)frame, sizeof(frame));
    if (len == sizeof(frame))
    {
        // Send succeeded
        ESP_LOGI(TAG, "LED control command sent successfully");
        return ESP_OK;
    }
    else
    {
        // Send failed
        ESP_LOGE(TAG, "Sending failed, actual bytes sent: %d", len);
        return ESP_FAIL;
    }
}

/**
 * @brief Colorful running light control function for fingerprint module (7-color cycle mode)
 * @param startColor Start color configuration (refer to LED_xxx macros, only lower 3 bits valid)
 * @param timeBit Breathing cycle time parameter (1-100, corresponding to 0.1s-10s)
 * @param cycleTimes Cycle times (0 = infinite cycle)
 * @return esp_err_t Operation result: ESP_OK = command sent successfully, ESP_FAIL = invalid parameters or command sent failed
 */
static esp_err_t control_colorful_led(uint8_t startColor, uint8_t timeBit, uint8_t cycleTimes)
{
    // Parameter validity check
    if (timeBit < 1 || timeBit > 100)
    {
        ESP_LOGE(TAG, "Colorful light control failed: Invalid time parameter (1-100 required, current %u)", timeBit);
        return ESP_FAIL;
    }
    // Filter invalid bits of color parameters
    if ((startColor & 0xF8) != 0)
    {
        ESP_LOGE(TAG, "Colorful light control failed: Only lower 3 bits of start color are valid, filtered to 0x%02X\n", startColor & 0x07);
        startColor &= 0x07;
    }
    // Construct data frame
    uint8_t frame[17] = {
        FRAME_HEADER[0], FRAME_HEADER[1],                                                               // Frame header (2 bytes)
        zw111.deviceAddress[0], zw111.deviceAddress[1], zw111.deviceAddress[2], zw111.deviceAddress[3], // Device address (4 bytes)
        PACKET_CMD,                                                                                     // Packet identifier (1 byte)
        0x00, 0x08,                                                                                     // Data length (2 bytes, fixed 8 bytes)
        CMD_CONTROL_BLN,                                                                                // Command code (1 byte)
        BLN_COLORFUL,                                                                                   // Function code (1 byte, colorful mode)
        startColor,                                                                                     // Start color (1 byte)
        0x11,                                                                                           // Fixed duty cycle value
        cycleTimes,                                                                                     // Cycle times (1 byte)
        timeBit,                                                                                        // Cycle time parameter (1 byte)
        0x00, 0x00                                                                                      // Checksum (2 bytes, to be calculated)
    };
    // Calculate and fill checksum
    uint16_t checksum = calculate_checksum(frame, sizeof(frame));
    frame[15] = (uint8_t)(checksum >> 8);   // High byte of checksum
    frame[16] = (uint8_t)(checksum & 0xFF); // Low byte of checksum

    // Send command via UART
    int len = uart_write_bytes(EX_UART_NUM, (const char *)frame, sizeof(frame));
    if (len == sizeof(frame))
    {
        // Send succeeded
        ESP_LOGI(TAG, "Colorful light control command sent successfully");
        return ESP_OK;
    }
    else
    {
        // Send failed
        ESP_LOGE(TAG, "Sending failed, actual bytes sent: %d", len);
        return ESP_FAIL;
    }
}

/**
 * @brief Delete specified number of fingerprints (delete continuously from specified ID)
 * @param ID Start fingerprint ID (0-99, returns failure if out of range)
 * @param count Number of fingerprints to delete (1-100, must not exceed ID range)
 * @return esp_err_t Operation result: ESP_OK = command sent successfully, ESP_FAIL = invalid parameters or command sent failed
 */
static esp_err_t delete_char(uint16_t ID, uint16_t count)
{
    // Parameter validity check
    if (ID >= 100)
    {
        // ID out of range
        ESP_LOGE(TAG, "Deletion failed: Start ID out of range (0-99 required, current %u)", ID);
        return ESP_FAIL;
    }
    if (count == 0 || count > 100 || (ID + count) > 100)
    {
        ESP_LOGE(TAG, "Deletion failed: Invalid count (1-100 required and no exceed ID range, current count %u)", count);
        // Invalid count or exceed ID range
        return ESP_FAIL;
    }
    // Construct data frame
    uint8_t frame[16] = {
        FRAME_HEADER[0], FRAME_HEADER[1],                                                               // Frame header (2 bytes)
        zw111.deviceAddress[0], zw111.deviceAddress[1], zw111.deviceAddress[2], zw111.deviceAddress[3], // Device address (4 bytes)
        PACKET_CMD,                                                                                     // Packet identifier (1 byte)
        0x00, 0x07,                                                                                     // Data length (2 bytes, fixed 7 bytes)
        CMD_DELET_CHAR,                                                                                 // Command code (1 byte)
        (uint8_t)(ID >> 8), (uint8_t)ID,                                                                // Start ID (2 bytes, high byte first)
        (uint8_t)(count >> 8), (uint8_t)count,                                                          // Delete count (2 bytes, high byte first)
        0x00, 0x00                                                                                      // Checksum (2 bytes, to be calculated)
    };
    // Calculate and fill checksum
    uint16_t checksum = calculate_checksum(frame, sizeof(frame));
    frame[14] = (uint8_t)(checksum >> 8);   // High byte of checksum
    frame[15] = (uint8_t)(checksum & 0xFF); // Low byte of checksum

    // Send command via UART
    int len = uart_write_bytes(EX_UART_NUM, (const char *)frame, sizeof(frame));
    if (len == sizeof(frame))
    {
        // Send succeeded
        ESP_LOGI(TAG, "Fingerprint deletion command sent successfully");
        return ESP_OK;
    }
    else
    {
        // Send failed
        ESP_LOGE(TAG, "Sending failed, actual bytes sent: %d", len);
        return ESP_FAIL;
    }
}

/**
 * @brief Clear all enrolled fingerprints in the module
 * @return esp_err_t Operation result: ESP_OK = command sent successfully, ESP_FAIL = command sent failed
 */
static esp_err_t empty()
{
    // Construct data frame
    uint8_t frame[12] = {
        FRAME_HEADER[0], FRAME_HEADER[1],                                                               // Frame header (2 bytes)
        zw111.deviceAddress[0], zw111.deviceAddress[1], zw111.deviceAddress[2], zw111.deviceAddress[3], // Device address (4 bytes)
        PACKET_CMD,                                                                                     // Packet identifier (1 byte)
        0x00, 0x03,                                                                                     // Data length (2 bytes)
        CMD_EMPTY,                                                                                      // Command code (1 byte)
        0x00, 0x00                                                                                      // Checksum (2 bytes, to be calculated)
    };
    // Calculate and fill checksum
    uint16_t checksum = calculate_checksum(frame, sizeof(frame));
    frame[10] = (uint8_t)(checksum >> 8);   // High byte of checksum
    frame[11] = (uint8_t)(checksum & 0xFF); // Low byte of checksum

    // Send command via UART
    int len = uart_write_bytes(EX_UART_NUM, (const char *)frame, sizeof(frame));
    if (len == sizeof(frame))
    {
        // Send succeeded
        ESP_LOGI(TAG, "Clear all fingerprints command sent successfully");
        return ESP_OK;
    }
    else
    {
        // Send failed
        ESP_LOGE(TAG, "Sending failed, actual bytes sent: %d", len);
        return ESP_FAIL;
    }
}

/**
 * @brief Cancel the current operation of the module (e.g., enrollment, identification)
 * @return esp_err_t Operation result: ESP_OK = command sent successfully, ESP_FAIL = command sent failed
 */
static esp_err_t cancel()
{
    // Construct data frame
    uint8_t frame[12] = {
        FRAME_HEADER[0], FRAME_HEADER[1],                                                               // Frame header (2 bytes)
        zw111.deviceAddress[0], zw111.deviceAddress[1], zw111.deviceAddress[2], zw111.deviceAddress[3], // Device address (4 bytes)
        PACKET_CMD,                                                                                     // Packet identifier (1 byte)
        0x00, 0x03,                                                                                     // Data length (2 bytes)
        CMD_CANCEL,                                                                                     // Command code (1 byte)
        0x00, 0x00                                                                                      // Checksum (2 bytes, to be calculated)
    };
    // Calculate and fill checksum
    uint16_t checksum = calculate_checksum(frame, sizeof(frame));
    frame[10] = (uint8_t)(checksum >> 8);   // High byte of checksum
    frame[11] = (uint8_t)(checksum & 0xFF); // Low byte of checksum

    // Send command via UART
    int len = uart_write_bytes(EX_UART_NUM, (const char *)frame, sizeof(frame));
    if (len == sizeof(frame))
    {
        // Send succeeded
        ESP_LOGI(TAG, "Cancel operation command sent successfully");
        return ESP_OK;
    }
    else
    {
        // Send failed
        ESP_LOGE(TAG, "Sending failed, actual bytes sent: %d", len);
        return ESP_FAIL;
    }
}

/**
 * @brief Control the module to enter sleep mode
 * @return esp_err_t Operation result: ESP_OK = command sent successfully, ESP_FAIL = command sent failed
 */
static esp_err_t sleep()
{
    // Construct data frame
    uint8_t frame[12] = {
        FRAME_HEADER[0], FRAME_HEADER[1],                                                               // Frame header (2 bytes)
        zw111.deviceAddress[0], zw111.deviceAddress[1], zw111.deviceAddress[2], zw111.deviceAddress[3], // Device address (4 bytes)
        PACKET_CMD,                                                                                     // Packet identifier (1 byte)
        0x00, 0x03,                                                                                     // Data length (2 bytes)
        CMD_SLEEP,                                                                                      // Command code (1 byte)
        0x00, 0x00                                                                                      // Checksum (2 bytes, to be calculated)
    };
    // Calculate and fill checksum
    uint16_t checksum = calculate_checksum(frame, sizeof(frame));
    frame[10] = (uint8_t)(checksum >> 8);   // High byte of checksum
    frame[11] = (uint8_t)(checksum & 0xFF); // Low byte of checksum

    // Send command via UART
    int len = uart_write_bytes(EX_UART_NUM, (const char *)frame, sizeof(frame));
    if (len == sizeof(frame))
    {
        // Send succeeded
        ESP_LOGI(TAG, "Sleep command sent successfully");
        return ESP_OK;
    }
    else
    {
        // Send failed
        ESP_LOGE(TAG, "Sending failed, actual bytes sent: %d", len);
        return ESP_FAIL;
    }
}

/**
 * @brief Read fingerprint index table from the module (get enrolled fingerprint IDs)
 * @param page Page number (0-4, each page corresponds to 20 fingerprints, total 100)
 * @return esp_err_t Operation result: ESP_OK = command sent successfully, ESP_FAIL = invalid parameters or command sent failed
 */
static esp_err_t read_index_table(uint8_t page)
{
    // Parameter validity check
    if (page > 4)
    {
        ESP_LOGE(TAG, "Invalid page number (0-4 required, current %u)", page);
        return ESP_FAIL;
    }
    // Construct data frame
    uint8_t frame[13] = {
        FRAME_HEADER[0], FRAME_HEADER[1],                                                               // Frame header (2 bytes)
        zw111.deviceAddress[0], zw111.deviceAddress[1], zw111.deviceAddress[2], zw111.deviceAddress[3], // Device address (4 bytes)
        PACKET_CMD,                                                                                     // Packet identifier (1 byte)
        0x00, 0x04,                                                                                     // Data length (2 bytes, fixed 4 bytes)
        CMD_READ_INDEX_TABLE,                                                                           // Command code (1 byte)
        page,                                                                                           // Page number (1 byte)
        0x00, 0x00                                                                                      // Checksum (2 bytes, to be calculated)
    };
    // Calculate and fill checksum
    uint16_t checksum = calculate_checksum(frame, sizeof(frame));
    frame[11] = (uint8_t)(checksum >> 8);   // High byte of checksum
    frame[12] = (uint8_t)(checksum & 0xFF); // Low byte of checksum

    // Send command via UART
    int len = uart_write_bytes(EX_UART_NUM, (const char *)frame, sizeof(frame));
    if (len == sizeof(frame))
    {
        // Send succeeded
        ESP_LOGI(TAG, "Read index table command sent successfully");
        return ESP_OK;
    }
    else
    {
        // Send failed
        ESP_LOGE(TAG, "Sending failed, actual bytes sent: %d", len);
        return ESP_FAIL;
    }
}

/**
 * @brief Parse the return data of read index table command and extract enrolled fingerprint IDs
 * @param receive_data Received data packet buffer
 * @param data_length Actual number of received bytes (must be explicitly passed)
 * @return esp_err_t Parsing result: ESP_OK = parsing succeeded, ESP_FAIL = invalid data or parsing failed
 */
static esp_err_t fingerprint_parse_frame(const uint8_t *receive_data, uint16_t data_length)
{
    // Initialize fingerprint ID array (0xFF means unused)
    memset(zw111.fingerIDArray, 0xFF, sizeof(zw111.fingerIDArray));
    zw111.fingerNumber = 0;
    // Mask array (used to detect if each bit is set)
    uint8_t mask[] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};
    uint8_t tempCount = 0; // Temporary count variable
    // Parse data field (index table data starts from byte 10, 13 bytes total, corresponding to 104 bits, 100 valid bits actually)
    for (uint8_t i = 10; i <= 22; i++) // i=10 to 22 (13 bytes total)
    {
        uint8_t byteData = receive_data[i];
        if (byteData == 0)
            continue; // Skip bytes with no fingerprints
        // Detect each bit of current byte (corresponding to 8 fingerprint IDs)
        for (uint8_t j = 0; j < 8; j++)
        {
            if (byteData & mask[j])
            {
                // Calculate fingerprint ID: (byte offset)×8 + bit offset
                uint8_t fingerID = (i - 10) * 8 + j;
                if (fingerID < 100) // Only keep valid IDs (0-99)
                {
                    zw111.fingerIDArray[tempCount] = fingerID;
                    tempCount++;
                    if (tempCount >= 100)
                        break; // Stop when maximum capacity is reached
                }
            }
        }
        if (tempCount >= 100)
            break; // Stop when maximum capacity is reached
    }
    // Update valid fingerprint count
    zw111.fingerNumber = tempCount;
    if (zw111.fingerNumber > 0)
    {
        ESP_LOGI(TAG, "Detected %u enrolled fingerprint IDs: ", zw111.fingerNumber);
        for (size_t i = 0; i < zw111.fingerNumber; i++)
        {
            ESP_LOGI(TAG, "%u ", zw111.fingerIDArray[i]);
        }
    }
    else
    {
        ESP_LOGI(TAG, "No enrolled fingerprints detected");
    }
    return ESP_OK;
}

/**
 * Find the smallest unused ID in the fingerprint list and return it
 * @return Smallest unused fingerprint ID, returns 255 if no available ID
 */
uint8_t get_mini_unused_id()
{
    // Special case: no fingerprints exist, return 0 directly
    if (zw111.fingerNumber == 0)
    {
        return 0;
    }
    // Check if the first ID is 0, if not, 0 is unused
    if (zw111.fingerIDArray[0] > 0)
    {
        return 0;
    }
    // Traverse sorted ID array to find gaps in continuous sequence
    for (uint8_t i = 0; i < zw111.fingerNumber - 1; i++)
    {
        // There is a gap between current ID and next ID
        if (zw111.fingerIDArray[i + 1] > zw111.fingerIDArray[i] + 1)
        {
            return zw111.fingerIDArray[i] + 1;
        }
    }
    // All existing IDs are continuous, return next value of last ID
    uint8_t last_id = zw111.fingerIDArray[zw111.fingerNumber - 1];
    if (last_id + 1 < 100) // Ensure not exceed maximum supported ID range
    {
        return last_id + 1;
    }
    // All possible IDs are used
    return 255;
}

/**
 * Insert newly enrolled fingerprint ID into array while maintaining array order
 * @param new_id New fingerprint ID to insert (should be obtained via get_mini_unused_id())
 * @return ESP_OK for successful insertion, ESP_FAIL for failure
 */
static esp_err_t insert_fingerprint_id(uint8_t new_id)
{
    // Check ID validity
    if (new_id >= 100)
    {
        return ESP_FAIL; // Invalid ID
    }
    // Check if array is full
    if (zw111.fingerNumber >= 100)
    {
        return ESP_FAIL; // Maximum capacity reached, cannot insert
    }
    // Find insertion position
    uint8_t insert_pos = 0;
    while (insert_pos < zw111.fingerNumber &&
           zw111.fingerIDArray[insert_pos] < new_id)
    {
        insert_pos++;
    }
    // Move elements to make space for new ID
    for (uint8_t i = zw111.fingerNumber; i > insert_pos; i--)
    {
        zw111.fingerIDArray[i] = zw111.fingerIDArray[i - 1];
    }
    // Insert new ID
    zw111.fingerIDArray[insert_pos] = new_id;
    // Update fingerprint count
    zw111.fingerNumber++;
    ESP_LOGI(TAG, "Insert fingerprint ID %u succeeded", zw111.fingerIDArray[insert_pos]);
    return ESP_OK; // Insertion succeeded
}

/**
 * @brief Cancel current operation of the module and execute a specific command
 * @note This function will cancel the current fingerprint operation (e.g., enrollment, identification) and set the state to canceled
 * @return void
 */
void cancel_current_operation_and_execute_command()
{
    zw111.state = 0x0A; // Switch to cancel state
    // Send cancel command
    if (cancel() == ESP_OK)
    {
        ESP_LOGI(TAG, "Preparing to cancel current operation, module state switched to cancel state");
    }
    else
    {
        // Cancel operation failed
        ESP_LOGE(TAG, "Failed to cancel current operation");
    }
}

/**
 * @brief Initialize UART communication for fingerprint module
 * @return esp_err_t ESP_OK = initialization succeeded, others = failed
 */
static esp_err_t fingerprint_initialization_uart()
{
    esp_err_t ret = ESP_OK;
    // Check if driver is already installed
    if (uart_is_driver_installed(EX_UART_NUM))
    {
        ESP_LOGW(TAG, "UART driver already installed, no need to reinstall");
        return ESP_OK;
    }
    // Install UART driver
    ret = uart_driver_install(EX_UART_NUM, 1024, 1024, 5, &uart2_queue, 0);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "UART driver installation failed: 0x%x", ret);
        return ret;
    }
    // Configure UART parameters
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ret = uart_param_config(EX_UART_NUM, &uart_config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "UART parameter configuration failed: 0x%x", ret);
        uart_driver_delete(EX_UART_NUM);
        return ret;
    }
    // Set UART pins
    ret = uart_set_pin(EX_UART_NUM, FINGERPRINT_RX_PIN, FINGERPRINT_TX_PIN,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "UART pin configuration failed: 0x%x", ret);
        uart_driver_delete(EX_UART_NUM);
        return ret;
    }
    // Configure pattern detection
    ret = uart_enable_pattern_det_baud_intr(EX_UART_NUM, 0x55, 1, 9, 20, 0);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Pattern detection configuration failed: 0x%x", ret);
        uart_driver_delete(EX_UART_NUM);
        return ret;
    }
    // Reset pattern queue
    ret = uart_pattern_queue_reset(EX_UART_NUM, 5);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Pattern queue reset failed: 0x%x", ret);
        uart_driver_delete(EX_UART_NUM);
        return ret;
    }
    ESP_LOGI(TAG, "UART initialization succeeded");
    return ESP_OK;
}

/**
 * @brief Deinitialize UART communication for fingerprint module
 * @return esp_err_t ESP_OK = deinitialization succeeded, ESP_FAIL = deinitialization failed
 */
static esp_err_t fingerprint_deinitialization_uart()
{
    if (!uart_is_driver_installed(EX_UART_NUM))
    {
        ESP_LOGE(TAG, "UART driver not installed, cannot delete");
        return ESP_FAIL;
    }
    // Wait for TX data transmission completion
    esp_err_t ret = uart_wait_tx_done(EX_UART_NUM, 100); // Timeout 100 ticks
    if (ret == ESP_ERR_TIMEOUT)
    {
        ESP_LOGW(TAG, "TX buffer data not fully sent, force delete");
    }
    // Flush RX buffer
    uart_flush_input(EX_UART_NUM);
    // Delete event queue
    if (uart2_queue != NULL)
    {
        vQueueDelete(uart2_queue);
        uart2_queue = NULL;
    }
    // Delete UART driver
    ret = uart_driver_delete(EX_UART_NUM);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "UART driver deletion failed: 0x%x", ret);
        return ret;
    }
    ESP_LOGI(TAG, "UART driver deleted");
    return ESP_OK;
}

/**
 * @brief Turn on fingerprint module
 * @note This function will power on the fingerprint module and set the state to powered on
 * @return void
 */
void turn_on_fingerprint()
{
    gpio_set_level(FINGERPRINT_CTL_PIN, 0); // Power on fingerprint module
    fingerprint_initialization_uart();      // Initialize UART communication
    xTaskCreate(uart_task, "uart_task", 8192, NULL, 10, NULL);
    zw111.power = true;
    ESP_LOGI(TAG, "Fingerprint module powered on");
}

/**
 * @brief Prepare to turn off the module
 * @note This function will prepare to turn off the fingerprint module, send sleep command and set state to sleep
 * @return void
 */
void prepare_turn_off_fingerprint()
{
    zw111.state = 0x0B; // Switch to sleep state
    // Send sleep command
    if (sleep() == ESP_OK)
    {
        ESP_LOGI(TAG, "Preparing to sleep, module state switched to sleep state");
    }
    else
    {
        // Sleep operation failed
        ESP_LOGE(TAG, "Failed to sleep current operation");
    }
}

/**
 * @brief Touch interrupt service routine
 * @param arg Interrupt parameter (GPIO number passed in)
 * @return void
 */
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    if (gpio_num == FINGERPRINT_INT_PIN && gpio_get_level(FINGERPRINT_INT_PIN) == 1)
    {
        xSemaphoreGiveFromISR(fingerprint_semaphore, NULL);
    }
}

/**
 * @brief Initialize fingerprint module
 * @return esp_err_t ESP_OK = initialization succeeded, ESP_FAIL = invalid data or initialization failed
 */
esp_err_t fingerprint_initialization()
{
    fingerprint_semaphore = xSemaphoreCreateBinary(); // Only used to activate the module after touch detection
    if (g_gpio_isr_service_installed == false)
    {
        gpio_install_isr_service(0);
        g_gpio_isr_service_installed = true;
    }

    // Initialize UART communication
    if (fingerprint_initialization_uart() != ESP_OK)
    {
        return ESP_FAIL;
    }

    // Initialize fingerprint module data structure
    zw111.deviceAddress[0] = 0xFF;
    zw111.deviceAddress[1] = 0xFF;
    zw111.deviceAddress[2] = 0xFF;
    zw111.deviceAddress[3] = 0xFF;

    gpio_config_t zw111_int_gpio_config = {
        .pin_bit_mask = (1ULL << FINGERPRINT_INT_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_POSEDGE};
    gpio_config(&zw111_int_gpio_config);

    gpio_config_t fingerprint_ctl_gpio_config = {
        .pin_bit_mask = (1ULL << FINGERPRINT_CTL_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&fingerprint_ctl_gpio_config);

    gpio_set_level(FINGERPRINT_CTL_PIN, 0);

    gpio_isr_handler_add(FINGERPRINT_INT_PIN, gpio_isr_handler, (void *)FINGERPRINT_INT_PIN);
    ESP_LOGI(TAG, "zw111 interrupt gpio configured");

    // Create a task to handle UART event from ISR
    xTaskCreate(uart_task, "uart_task", 8192, NULL, 10, NULL);
    ESP_LOGI(TAG, "uart task created");

    // Create a task to handle fingerprint processing after touch detection
    xTaskCreate(fingerprint_task, "fingerprint_task", 8192, NULL, 10, NULL);
    ESP_LOGI(TAG, "fingerprint task created");

    return ESP_OK;
}

/**
 * @brief Fingerprint task
 * @param pvParameters Task parameters (unused)
 * @return void
 */
void fingerprint_task(void *pvParameters)
{
    while (1)
    {
        // Wait for semaphore to be released
        if (xSemaphoreTake(fingerprint_semaphore, portMAX_DELAY) == pdTRUE)
        {
            // Semaphore released, indicating fingerprint module is ready
            ESP_LOGI(TAG, "Fingerprint module is ready, start processing tasks");
            // Print current module state
            ESP_LOGI(TAG, "Fingerprint module power state: %s", zw111.power ? "Powered on" : "Powered off");
            ESP_LOGI(TAG, "Fingerprint module state: %s",
                     zw111.state == 0x00   ? "Initial state"
                     : zw111.state == 0x01 ? "Read index table state"
                     : zw111.state == 0x02 ? "Enroll fingerprint state"
                     : zw111.state == 0x03 ? "Delete fingerprint state"
                     : zw111.state == 0x04 ? "Verify fingerprint state"
                     : zw111.state == 0x0A ? "Cancel state"
                     : zw111.state == 0x0B ? "Sleep state"
                                           : "Unknown state");
            ESP_LOGI(TAG, "Fingerprint module device address: %02X:%02X:%02X:%02X",
                     zw111.deviceAddress[0], zw111.deviceAddress[1],
                     zw111.deviceAddress[2], zw111.deviceAddress[3]);
            ESP_LOGI(TAG, "Number of enrolled fingerprints in module: %u", zw111.fingerNumber);
            ESP_LOGI(TAG, "Enrolled fingerprint IDs in module: ");
            for (size_t i = 0; i < zw111.fingerNumber; i++)
            {
                ESP_LOGI(TAG, "%u ", zw111.fingerIDArray[i]);
            }
            // Start fingerprint verification
            if (zw111.power == false) // Power off state
            {
                ESP_LOGI(TAG, "Current state is power off, preparing to verify fingerprint");
                zw111.state = 0x04;    // Switch to verify fingerprint state
                turn_on_fingerprint(); // Power on fingerprint module
            }
            // Handle abnormal module state
            else if (zw111.power == true) // Power on state
            {
                ESP_LOGE(TAG, "Current state is abnormal, preparing to turn off fingerprint module");
                cancel_current_operation_and_execute_command(); // Cancel current operation
                prepare_turn_off_fingerprint();                 // Prepare to turn off fingerprint module
            }
        }
    }
}

/**
 * @brief UART event handling task
 * @param pvParameters Task parameters (unused)
 * @return void
 */
void uart_task(void *pvParameters)
{
    uart_event_t event;
    static uint8_t dtmp[1024];
    while (1)
    {
        if (xQueueReceive(uart2_queue, (void *)&event, portMAX_DELAY) == pdTRUE)
        {
            bzero(dtmp, 1024);
            size_t buffered_size;
            switch (event.type)
            {
            case UART_DATA:
                if (zw111.state == 0X0B && event.size == 12) // Sleep state
                {
                    // Receive data first
                    uart_read_bytes(EX_UART_NUM, dtmp, event.size, portMAX_DELAY);
                    // Verify if received data is valid
                    if (verify_received_data(dtmp, event.size) != ESP_OK)
                    {
                        ESP_LOGE(TAG, "Received invalid data, discarded");
                        break; // Discard invalid data
                    }
                    if (dtmp[9] == 0x00) // Confirm code = 00H means sleep setting succeeded
                    {
                        fingerprint_deinitialization_uart();    // Delete UART driver
                        zw111.power = false;                    // Set power state to false
                        zw111.state = 0X00;                     // Switch to initial state
                        gpio_set_level(FINGERPRINT_CTL_PIN, 1); // Power off fingerprint module
                        ESP_LOGI(TAG, "Fingerprint module powered off, state reset to initial state");
                        vTaskDelete(NULL); // Delete current task
                    }
                }
                else if (zw111.state == 0X0A && event.size == 12) // Cancel state
                {
                    // Receive data first
                    uart_read_bytes(EX_UART_NUM, dtmp, event.size, portMAX_DELAY);
                    // Verify if received data is valid
                    if (verify_received_data(dtmp, event.size) != ESP_OK)
                    {
                        ESP_LOGE(TAG, "Received invalid data, discarded");
                        break; // Discard invalid data
                    }
                    if (dtmp[9] == 0x00) // Confirm code = 00H means cancel operation succeeded
                    {
                        ESP_LOGI(TAG, "Cancel operation succeeded, preparing to execute other commands");
                        if (g_ready_add_fingerprint == true)
                        {
                            zw111.state = 0x02;              // Set state to enroll fingerprint state
                            g_ready_add_fingerprint = false; // Reset add fingerprint flag
                            // Send enroll fingerprint command
                            if (auto_enroll(get_mini_unused_id(), 5, false, false, false, false, true, false) != ESP_OK)
                            {
                                ESP_LOGE(TAG, "Failed to send enroll fingerprint command");
                                prepare_turn_off_fingerprint();
                            }
                        }
                        else if (g_cancel_add_fingerprint == true)
                        {
                            g_cancel_add_fingerprint = false; // Reset cancel add fingerprint flag
                            prepare_turn_off_fingerprint();   // Prepare to turn off fingerprint module
                        }
                        else if (g_ready_delete_fingerprint == true && g_ready_delete_all_fingerprint == false)
                        {
                            zw111.state = 0x03; // Set state to delete fingerprint state
                            // Send delete fingerprint command
                            if (delete_char(g_deleteFingerprintID, 1) != ESP_OK)
                            {
                                ESP_LOGE(TAG, "Failed to send delete fingerprint command");
                                prepare_turn_off_fingerprint(); // Prepare to turn off fingerprint module
                            }
                        }
                        else if (g_ready_delete_all_fingerprint == true && g_ready_delete_fingerprint == false)
                        {
                            zw111.state = 0x03; // Set state to delete fingerprint state
                            // Send delete all fingerprints command
                            if (empty() != ESP_OK)
                            {
                                ESP_LOGE(TAG, "Failed to send delete all fingerprints command");
                                prepare_turn_off_fingerprint(); // Prepare to turn off fingerprint module
                            }
                        }
                        else
                        {
                            prepare_turn_off_fingerprint(); // Prepare to turn off fingerprint module
                        }
                    }
                }
                else if (zw111.state == 0X04 && event.size == 17) // Verify fingerprint state
                {
                    // Receive data first
                    uart_read_bytes(EX_UART_NUM, dtmp, event.size, portMAX_DELAY);
                    // Verify if received data is valid
                    if (verify_received_data(dtmp, event.size) != ESP_OK)
                    {
                        ESP_LOGE(TAG, "Received invalid data, discarded");
                        break; // Discard invalid data
                    }
                    if (dtmp[10] == 0x00 && dtmp[9] == 0x00)
                    {
                        ESP_LOGI(TAG, "Verify fingerprint - Command executed successfully, waiting for image capture");
                    }
                    else if (dtmp[10] == 0x01)
                    {
                        if (dtmp[9] == 0x00)
                        {
                            ESP_LOGI(TAG, "Verify fingerprint - Image capture succeeded");
                        }
                        else if (dtmp[9] == 0x26)
                        {
                            ESP_LOGW(TAG, "Verify fingerprint - Image capture timeout");
                            prepare_turn_off_fingerprint(); // Prepare to turn off fingerprint module
                        }
                    }
                    else if (dtmp[10] == 0x05)
                    {
                        if (dtmp[9] == 0x00)
                        {
                            uint8_t message = 0x01;
                            xQueueSend(fingerprint_queue, &message, portMAX_DELAY);
                            uint16_t fingerID = (dtmp[11] << 8) | dtmp[12]; // Fingerprint ID
                            uint16_t score = (dtmp[13] << 8) | dtmp[14];    // Matching score
                            ESP_LOGI(TAG, "Verify fingerprint - Fingerprint found, ID: %u, Score: %u", fingerID, score);
                        }
                        else if (dtmp[9] == 0x09)
                        {
                            ESP_LOGI(TAG, "Verify fingerprint - No fingerprint found");
                            uint8_t message = 0x00;
                            xQueueSend(fingerprint_queue, &message, portMAX_DELAY);
                        }
                        else if (dtmp[9] == 0x24)
                        {
                            ESP_LOGW(TAG, "Verify fingerprint - Fingerprint library is empty");
                            uint8_t message = 0x00;
                            xQueueSend(fingerprint_queue, &message, portMAX_DELAY);
                        }
                    }
                    else if (dtmp[10] == 0x02 && dtmp[9] == 0x09)
                    {
                        ESP_LOGW(TAG, "Verify fingerprint - No finger on sensor");
                        uint8_t message = 0x00;
                        xQueueSend(fingerprint_queue, &message, portMAX_DELAY);
                    }
                    else
                    {
                        ESP_LOGE(TAG, "Verify fingerprint - Unknown data, discarded");
                        prepare_turn_off_fingerprint(); // Prepare to turn off fingerprint module
                    }
                }
                else if (zw111.state == 0X01 && event.size == 44) // Read index table state
                {
                    // Receive data first
                    uart_read_bytes(EX_UART_NUM, dtmp, event.size, portMAX_DELAY);
                    // Verify if received data is valid
                    if (verify_received_data(dtmp, event.size) != ESP_OK)
                    {
                        ESP_LOGE(TAG, "Received invalid data, discarded");
                        break; // Discard invalid data
                    }
                    ESP_LOGI(TAG, "Received index table data, length: %u", event.size);
                    fingerprint_parse_frame(dtmp, event.size); // Parse fingerprint index table data
                    prepare_turn_off_fingerprint();            // Prepare to turn off fingerprint module
                }
                else if (zw111.state == 0X02 && event.size == 14) // Enroll fingerprint state
                {
                    // Receive data first
                    uart_read_bytes(EX_UART_NUM, dtmp, event.size, portMAX_DELAY);
                    // Verify if received data is valid
                    if (verify_received_data(dtmp, event.size) != ESP_OK)
                    {
                        ESP_LOGE(TAG, "Received invalid data, discarded");
                        break; // Discard invalid data
                    }
                    if (dtmp[10] == 0x00 && dtmp[11] == 0x00)
                    {
                        if (dtmp[9] == 0x00)
                        {
                            ESP_LOGI(TAG, "Enroll fingerprint - Command executed successfully, waiting for image capture");
                        }
                        else if (dtmp[9] == 0x22)
                        {
                            send_operation_result("fingerprint_added", false);
                            ESP_LOGE(TAG, "Enroll fingerprint - Current ID is already in use, please select another ID");
                            prepare_turn_off_fingerprint(); // Prepare to turn off fingerprint module
                        }
                        else
                        {
                            send_operation_result("fingerprint_added", false);
                            ESP_LOGE(TAG, "Enroll fingerprint - Unknown data, discarded");
                            prepare_turn_off_fingerprint(); // Prepare to turn off fingerprint module
                        }
                    }
                    else if (dtmp[10] == 0x01)
                    {
                        if (dtmp[9] == 0x00)
                        {
                            ESP_LOGI(TAG, "Enroll fingerprint - %uth image capture succeeded", dtmp[11]);
                        }
                        else if (dtmp[9] == 0x26)
                        {
                            send_operation_result("fingerprint_added", false);
                            ESP_LOGI(TAG, "Enroll fingerprint - %uth image capture timeout", dtmp[11]);
                            prepare_turn_off_fingerprint(); // Prepare to turn off fingerprint module
                        }
                        else
                        {
                            send_operation_result("fingerprint_added", false);
                            ESP_LOGI(TAG, "Enroll fingerprint - %uth image capture failed", dtmp[11]);
                            prepare_turn_off_fingerprint(); // Prepare to turn off fingerprint module
                        }
                    }
                    else if (dtmp[10] == 0x02)
                    {
                        if (dtmp[9] == 0x00)
                        {
                            ESP_LOGI(TAG, "Enroll fingerprint - %uth feature generation succeeded", dtmp[11]);
                        }
                        else if (dtmp[9] == 0x26)
                        {
                            send_operation_result("fingerprint_added", false);
                            ESP_LOGI(TAG, "Enroll fingerprint - %uth feature generation timeout", dtmp[11]);
                            prepare_turn_off_fingerprint(); // Prepare to turn off fingerprint module
                        }
                        else
                        {
                            send_operation_result("fingerprint_added", false);
                            ESP_LOGI(TAG, "Enroll fingerprint - %uth image capture failed", dtmp[11]);
                            prepare_turn_off_fingerprint(); // Prepare to turn off fingerprint module
                        }
                    }
                    else if (dtmp[10] == 0x03)
                    {
                        if (dtmp[9] == 0x00)
                        {
                            ESP_LOGI(TAG, "Enroll fingerprint - Finger removed %uth time, enrollment succeeded", dtmp[11]);
                        }
                        else if (dtmp[9] == 0x26)
                        {
                            send_operation_result("fingerprint_added", false);
                            ESP_LOGI(TAG, "Enroll fingerprint - Finger removed %uth time, enrollment timeout", dtmp[11]);
                            prepare_turn_off_fingerprint(); // Prepare to turn off fingerprint module
                        }
                        else
                        {
                            send_operation_result("fingerprint_added", false);
                            ESP_LOGI(TAG, "Enroll fingerprint - Finger removed %uth time, enrollment failed", dtmp[11]);
                            prepare_turn_off_fingerprint(); // Prepare to turn off fingerprint module
                        }
                    }
                    else if (dtmp[10] == 0x04 && dtmp[11] == 0xF0)
                    {
                        if (dtmp[9] == 0x00)
                        {
                            ESP_LOGI(TAG, "Enroll fingerprint - Template merging succeeded", dtmp[11]);
                        }
                        else if (dtmp[9] == 0x26)
                        {
                            send_operation_result("fingerprint_added", false);
                            ESP_LOGI(TAG, "Enroll fingerprint - Template merging timeout", dtmp[11]);
                            prepare_turn_off_fingerprint(); // Prepare to turn off fingerprint module
                        }
                        else
                        {
                            send_operation_result("fingerprint_added", false);
                            ESP_LOGI(TAG, "Enroll fingerprint - Template merging failed", dtmp[11]);
                            prepare_turn_off_fingerprint(); // Prepare to turn off fingerprint module
                        }
                    }
                    else if (dtmp[10] == 0x05 && dtmp[11] == 0xF1)
                    {
                        if (dtmp[9] == 0x00)
                        {
                            ESP_LOGI(TAG, "Enroll fingerprint - Enrollment detection passed");
                        }
                        else if (dtmp[9] == 0x26)
                        {
                            send_operation_result("fingerprint_added", false);
                            ESP_LOGI(TAG, "Enroll fingerprint - Enrollment detection timeout");
                            prepare_turn_off_fingerprint(); // Prepare to turn off fingerprint module
                        }
                        else
                        {
                            send_operation_result("fingerprint_added", false);
                            ESP_LOGI(TAG, "Enroll fingerprint - Enrollment detection failed", dtmp[11]);
                            prepare_turn_off_fingerprint(); // Prepare to turn off fingerprint module
                        }
                    }
                    else if (dtmp[10] == 0x06 && dtmp[11] == 0xF2)
                    {
                        if (dtmp[9] == 0x00)
                        {
                            send_operation_result("fingerprint_added", true);
                            ESP_LOGI(TAG, "Enroll fingerprint - Template storage succeeded, ID: %u", get_mini_unused_id());
                            insert_fingerprint_id(get_mini_unused_id());
                            send_fingerprint_list();
                            prepare_turn_off_fingerprint(); // Prepare to turn off fingerprint module
                        }
                        else if (dtmp[9] == 0x26)
                        {
                            send_operation_result("fingerprint_added", false);
                            ESP_LOGI(TAG, "Enroll fingerprint - Template storage timeout");
                            prepare_turn_off_fingerprint(); // Prepare to turn off fingerprint module
                        }
                        else
                        {
                            send_operation_result("fingerprint_added", false);
                            ESP_LOGI(TAG, "Enroll fingerprint - Template storage failed");
                            prepare_turn_off_fingerprint(); // Prepare to turn off fingerprint module
                        }
                    }
                }
                else if (zw111.state == 0X03 && event.size == 12) // Delete fingerprint state
                {
                    // Receive data first
                    uart_read_bytes(EX_UART_NUM, dtmp, event.size, portMAX_DELAY);
                    // Verify if received data is valid
                    if (verify_received_data(dtmp, event.size) != ESP_OK)
                    {
                        ESP_LOGE(TAG, "Received invalid data, discarded");
                        break; // Discard invalid data
                    }
                    // Handle clear all fingerprints
                    if (g_ready_delete_fingerprint == false && g_ready_delete_all_fingerprint == true)
                    {
                        for (size_t i = 0; i <= zw111.fingerNumber; i++)
                        {
                            zw111.fingerIDArray[i] = 0xFF; // Clear fingerprint IDs
                        }
                        send_operation_result("fingerprint_cleared", true);
                        zw111.fingerNumber = 0;                 // Clear fingerprint count
                        g_ready_delete_all_fingerprint = false; // Reset delete all fingerprints flag
                        ESP_LOGI(TAG, "Delete fingerprint - Clear all fingerprints succeeded");
                    }
                    // Handle delete single fingerprint
                    else if (g_ready_delete_fingerprint == true && g_ready_delete_all_fingerprint == false)
                    {
                        // Find position of target ID
                        size_t i;
                        for (i = 0; i < zw111.fingerNumber; i++)
                        {
                            if (zw111.fingerIDArray[i] == g_deleteFingerprintID)
                            {
                                break; // Target found, exit loop to prepare deletion
                            }
                        }
                        // Shift elements forward to fill the gap
                        for (size_t j = i; j < zw111.fingerNumber - 1; j++)
                        {
                            zw111.fingerIDArray[j] = zw111.fingerIDArray[j + 1];
                        }
                        zw111.fingerIDArray[zw111.fingerNumber - 1] = 0xFF; // Reset last position to 0xFF
                        zw111.fingerNumber--;                               // Decrease fingerprint count
                        send_operation_result("fingerprint_deleted", true);
                        send_fingerprint_list();
                        g_ready_delete_fingerprint = false; // Reset delete single fingerprint flag
                        ESP_LOGI(TAG, "Delete fingerprint - Delete ID:%u succeeded", g_deleteFingerprintID);
                    }
                    prepare_turn_off_fingerprint(); // Prepare to turn off fingerprint module
                }
                break;
            case UART_PATTERN_DET:
                uart_get_buffered_data_len(EX_UART_NUM, &buffered_size);
                int pos = uart_pattern_pop_pos(EX_UART_NUM);
                ESP_LOGI(TAG, "[UART PATTERN DETECTED] pos: %d, buffered size: %u", pos, buffered_size);
                if (pos == -1)
                {
                    // There used to be a UART_PATTERN_DET event, but the pattern position queue is full so that it can not
                    // record the position. We should set a larger queue size.
                    // As an example, we directly flush the rx buffer here.
                    uart_flush_input(EX_UART_NUM);
                }
                else
                {
                    uart_read_bytes(EX_UART_NUM, dtmp, pos, pdMS_TO_TICKS(100));
                    uint8_t pat[2];
                    memset(pat, 0, sizeof(pat));
                    uart_read_bytes(EX_UART_NUM, pat, 1, pdMS_TO_TICKS(100));
                    if (pat[0] == 0X55)
                    {
                        ESP_LOGI(TAG, "Fingerprint module just powered on, state: %s",
                                 zw111.state == 0x00   ? "Initial state"
                                 : zw111.state == 0x01 ? "Read index table state"
                                 : zw111.state == 0x02 ? "Enroll fingerprint state"
                                 : zw111.state == 0x03 ? "Delete fingerprint state"
                                 : zw111.state == 0x04 ? "Verify fingerprint state"
                                 : zw111.state == 0x0A ? "Cancel state"
                                 : zw111.state == 0x0B ? "Sleep state"
                                                       : "Unknown state");
                        if (zw111.state == 0X04) // Verify fingerprint state
                        {
                            // Send verify fingerprint command
                            if (auto_identify(0xFFFF, 2, false, false, false) != ESP_OK)
                            {
                                ESP_LOGE(TAG, "Failed to send verify fingerprint command");
                                prepare_turn_off_fingerprint(); // Prepare to turn off fingerprint module
                            }
                        }
                        else if (zw111.state == 0X00) // Just powered on state
                        {
                            zw111.state = 0X01; // Switch to read index table state
                            read_index_table(0);
                        }
                        else if (zw111.state == 0X02) // Enroll fingerprint state
                        {
                            ESP_LOGI(TAG, "Fingerprint module in enrollment state, preparing to enroll fingerprint, ID:%u", get_mini_unused_id());
                            // Send enroll fingerprint command
                            if (auto_enroll(get_mini_unused_id(), 5, false, false, false, false, true, false) != ESP_OK)
                            {
                                ESP_LOGE(TAG, "Failed to send enroll fingerprint command");
                                prepare_turn_off_fingerprint(); // Prepare to turn off fingerprint module
                            }
                        }
                        else if (zw111.state == 0X03) // Delete fingerprint state
                        {
                            if (g_ready_delete_fingerprint == true && g_ready_delete_all_fingerprint == false)
                            {
                                // Delete single fingerprint
                                if (delete_char(g_deleteFingerprintID, 1) != ESP_OK)
                                {
                                    ESP_LOGE(TAG, "Failed to send delete fingerprint command");
                                    prepare_turn_off_fingerprint(); // Prepare to turn off fingerprint module
                                }
                            }
                            else if (g_ready_delete_fingerprint == false && g_ready_delete_all_fingerprint == true)
                            {
                                // Delete all fingerprints
                                if (empty() != ESP_OK)
                                {
                                    ESP_LOGE(TAG, "Failed to send delete all fingerprints command");
                                    prepare_turn_off_fingerprint(); // Prepare to turn off fingerprint module
                                }
                            }
                        }
                    }
                }
                break;
            default:
                break;
            }
        }
    }
}
