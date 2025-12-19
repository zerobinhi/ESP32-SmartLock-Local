#ifndef NVS_CUSTOM_H
#define NVS_CUSTOM_H

#include "nvs_flash.h"
#include "app_config.h"

/**
 * @brief 初始化默认NVS分区
 * @note 整个项目启动后调用1次即可，若分区损坏会自动尝试擦除后重新初始化
 * @return esp_err_t 错误码：ESP_OK成功，其他为失败
 */
esp_err_t nvs_custom_init(void);

/**
 * @brief 反初始化默认NVS分区
 * @note 仅在需要释放NVS资源时调用
 * @return esp_err_t 错误码：ESP_OK成功，其他为失败
 */
esp_err_t nvs_custom_deinit(void);

// ========================== 无符号整数类型 ==========================
/**
 * @brief 写入uint8_t到NVS
 * @param part_name NVS分区名
 * @param ns_name 命名空间名
 * @param key 数据键名
 * @param value 要写入的uint8_t值
 * @return esp_err_t 错误码：ESP_OK成功，其他为失败
 */
esp_err_t nvs_custom_set_u8(const char *part_name, const char *ns_name, const char *key, uint8_t value);

/**
 * @brief 读取uint8_t从NVS
 * @param part_name NVS分区名
 * @param ns_name 命名空间名
 * @param key 数据键名
 * @param out_value 输出缓冲区
 * @return esp_err_t 错误码：ESP_OK成功，ESP_ERR_NVS_NOT_FOUND=键不存在
 */
esp_err_t nvs_custom_get_u8(const char *part_name, const char *ns_name, const char *key, uint8_t *out_value);

/**
 * @brief 写入uint16_t到NVS
 * @param part_name NVS分区名
 * @param ns_name 命名空间名
 * @param key 数据键名
 * @param value 要写入的uint16_t值
 * @return esp_err_t 错误码
 */
esp_err_t nvs_custom_set_u16(const char *part_name, const char *ns_name, const char *key, uint16_t value);

/**
 * @brief 读取uint16_t从NVS
 * @param part_name NVS分区名
 * @param ns_name 命名空间名
 * @param key 数据键名
 * @param out_value 输出缓冲区
 * @return esp_err_t 错误码
 */
esp_err_t nvs_custom_get_u16(const char *part_name, const char *ns_name, const char *key, uint16_t *out_value);

/**
 * @brief 写入uint32_t到NVS
 * @param part_name NVS分区名
 * @param ns_name 命名空间名
 * @param key 数据键名
 * @param value 要写入的uint32_t值
 * @return esp_err_t 错误码
 */
esp_err_t nvs_custom_set_u32(const char *part_name, const char *ns_name, const char *key, uint32_t value);

/**
 * @brief 读取uint32_t从NVS
 * @param part_name NVS分区名
 * @param ns_name 命名空间名
 * @param key 数据键名
 * @param out_value 输出缓冲区
 * @return esp_err_t 错误码
 */
esp_err_t nvs_custom_get_u32(const char *part_name, const char *ns_name, const char *key, uint32_t *out_value);

/**
 * @brief 写入uint64_t到NVS
 * @param part_name NVS分区名
 * @param ns_name 命名空间名
 * @param key 数据键名
 * @param value 要写入的uint64_t值
 * @return esp_err_t 错误码
 */
esp_err_t nvs_custom_set_u64(const char *part_name, const char *ns_name, const char *key, uint64_t value);

/**
 * @brief 读取uint64_t从NVS
 * @param part_name NVS分区名
 * @param ns_name 命名空间名
 * @param key 数据键名
 * @param out_value 输出缓冲区
 * @return esp_err_t 错误码
 */
esp_err_t nvs_custom_get_u64(const char *part_name, const char *ns_name, const char *key, uint64_t *out_value);

// ========================== 有符号整数类型 ==========================
/**
 * @brief 写入int8_t到NVS
 * @param part_name NVS分区名
 * @param ns_name 命名空间名
 * @param key 数据键名
 * @param value 要写入的int8_t值
 * @return esp_err_t 错误码
 */
esp_err_t nvs_custom_set_i8(const char *part_name, const char *ns_name, const char *key, int8_t value);

/**
 * @brief 读取int8_t从NVS
 * @param part_name NVS分区名
 * @param ns_name 命名空间名
 * @param key 数据键名
 * @param out_value 输出缓冲区
 * @return esp_err_t 错误码
 */
esp_err_t nvs_custom_get_i8(const char *part_name, const char *ns_name, const char *key, int8_t *out_value);

/**
 * @brief 写入int16_t到NVS
 * @param part_name NVS分区名
 * @param ns_name 命名空间名
 * @param key 数据键名
 * @param value 要写入的int16_t值
 * @return esp_err_t 错误码
 */
esp_err_t nvs_custom_set_i16(const char *part_name, const char *ns_name, const char *key, int16_t value);

/**
 * @brief 读取int16_t从NVS
 * @param part_name NVS分区名
 * @param ns_name 命名空间名
 * @param key 数据键名
 * @param out_value 输出缓冲区
 * @return esp_err_t 错误码
 */
esp_err_t nvs_custom_get_i16(const char *part_name, const char *ns_name, const char *key, int16_t *out_value);

/**
 * @brief 写入int32_t到NVS
 * @param part_name NVS分区名
 * @param ns_name 命名空间名
 * @param key 数据键名
 * @param value 要写入的int32_t值
 * @return esp_err_t 错误码
 */
esp_err_t nvs_custom_set_i32(const char *part_name, const char *ns_name, const char *key, int32_t value);

/**
 * @brief 读取int32_t从NVS
 * @param part_name NVS分区名
 * @param ns_name 命名空间名
 * @param key 数据键名
 * @param out_value 输出缓冲区
 * @return esp_err_t 错误码
 */
esp_err_t nvs_custom_get_i32(const char *part_name, const char *ns_name, const char *key, int32_t *out_value);

/**
 * @brief 写入int64_t到NVS
 * @param part_name NVS分区名
 * @param ns_name 命名空间名
 * @param key 数据键名
 * @param value 要写入的int64_t值
 * @return esp_err_t 错误码
 */
esp_err_t nvs_custom_set_i64(const char *part_name, const char *ns_name, const char *key, int64_t value);

/**
 * @brief 读取int64_t从NVS
 * @param part_name NVS分区名
 * @param ns_name 命名空间名
 * @param key 数据键名
 * @param out_value 输出缓冲区
 * @return esp_err_t 错误码
 */
esp_err_t nvs_custom_get_i64(const char *part_name, const char *ns_name, const char *key, int64_t *out_value);

// ========================== 其他常用类型 ==========================
/**
 * @brief 写入字符串到NVS
 * @param part_name NVS分区名
 * @param ns_name 命名空间名
 * @param key 数据键名
 * @param value 要写入的字符串（需以'\0'结尾）
 * @return esp_err_t 错误码
 */
esp_err_t nvs_custom_set_str(const char *part_name, const char *ns_name, const char *key, const char *value);

/**
 * @brief 读取字符串从NVS
 * @param part_name NVS分区名
 * @param ns_name 命名空间名
 * @param key 数据键名
 * @param out_buf 输出缓冲区（存储读取到的字符串）
 * @param buf_len 输入：缓冲区长度；输出：实际字符串长度（含'\0'）
 * @return esp_err_t 错误码：ESP_ERR_NVS_INVALID_LENGTH=缓冲区不足
 */
esp_err_t nvs_custom_get_str(const char *part_name, const char *ns_name, const char *key, char *out_buf, size_t *buf_len);

/**
 * @brief 删除NVS中指定键
 * @param part_name NVS分区名
 * @param ns_name 命名空间名
 * @param key 要删除的键名
 * @return esp_err_t 错误码
 */
esp_err_t nvs_custom_erase_key(const char *part_name, const char *ns_name, const char *key);

/**
 * @brief 写入二进制大对象（blob）到NVS
 * @param part_name NVS分区名
 * @param ns_name 命名空间名
 * @param key 数据键名
 * @param value 指向要写入数据的指针
 * @param value_size 要写入数据的字节大小
 * @return esp_err_t 错误码
 */
esp_err_t nvs_custom_set_blob(const char *part_name, const char *ns_name, const char *key, const void *value, size_t value_size);

/**
 * @brief 读取二进制大对象（blob）从NVS
 * @param part_name NVS分区名
 * @param ns_name 命名空间名
 * @param key 数据键名
 * @param out_buf 输出缓冲区（存储读取到的数据）
 * @param buf_size 输入：缓冲区大小；输出：实际读取到的数据大小
 * @return esp_err_t 错误码：ESP_ERR_NVS_INVALID_LENGTH=缓冲区不足
 */
esp_err_t nvs_custom_get_blob(const char *part_name, const char *ns_name, const char *key, void *out_buf, size_t *buf_size);

/**
 * @brief 删除NVS中指定命名空间的所有键
 * @param part_name NVS分区名
 * @param ns_name 命名空间名
 * @return esp_err_t 错误码
 */
esp_err_t nvs_custom_erase_all(const char *part_name, const char *ns_name);

/**
 * @brief 擦除整个NVS分区（恢复出厂设置用）
 * @param part_name 分区名（NULL使用默认分区）
 * @return esp_err_t
 */
esp_err_t nvs_custom_erase_partition(const char *part_name);

/**
 * @brief 检查指定键是否存在
 * @param part_name 分区名（NULL使用默认）
 * @param ns_name 命名空间
 * @param key 键名
 * @return true 存在, false 不存在
 */
bool nvs_custom_key_exists(const char *part_name, const char *ns_name, const char *key);

/**
 * @brief 获取NVS分区使用情况
 * @param part_name 分区名（NULL使用默认）
 * @param out_stats 输出统计信息
 * @return esp_err_t
 */
esp_err_t nvs_custom_get_stats(const char *part_name, nvs_stats_t *out_stats);

#endif
