#include "nvs_custom.h"

static const char *TAG = "nvs_custom";

// -------------------------- 初始化/反初始化 --------------------------
/**
 * @brief 初始化默认NVS分区（复用原生nvs_flash_init）
 */
esp_err_t nvs_custom_init(void)
{
    esp_err_t ret = nvs_flash_init();
    // 处理分区版本不匹配或无空闲页的情况（原生逻辑）
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "NVS partition need erase, try to erase...");
        ret = nvs_flash_erase(); // 擦除分区
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Erase NVS partition failed: 0x%x", ret);
            return ret;
        }
        ret = nvs_flash_init(); // 重新初始化
    }
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "NVS init success (partition: %s)", NVS_DEFAULT_PART_NAME);
    }
    else
    {
        ESP_LOGE(TAG, "NVS init failed: 0x%x", ret);
    }
    return ret;
}

/**
 * @brief 反初始化默认NVS分区（复用原生nvs_flash_deinit）
 */
esp_err_t nvs_custom_deinit(void)
{
    esp_err_t ret = nvs_flash_deinit();
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "NVS deinit success");
    }
    else
    {
        ESP_LOGE(TAG, "NVS deinit failed: 0x%x", ret);
    }
    return ret;
}

// -------------------------- 通用辅助函数：打开/关闭命名空间 --------------------------
/**
 * @brief 辅助函数：打开NVS命名空间（内部使用，封装原生nvs_open_from_partition）
 * @param part_name 分区名（NULL则用默认）
 * @param ns_name 命名空间名
 * @param open_mode 打开模式（NVS_READONLY/NVS_READWRITE）
 * @param out_handle 输出handle
 * @return esp_err_t 错误码
 */
static esp_err_t __nvs_custom_open(const char *part_name, const char *ns_name,
                                   nvs_open_mode_t open_mode, nvs_handle_t *out_handle)
{
    // 参数合法性检查（避免原生接口崩溃）
    if (ns_name == NULL || out_handle == NULL)
    {
        ESP_LOGE(TAG, "Invalid param: ns_name or out_handle is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    // 若分区名为NULL，使用原生默认分区（NVS_DEFAULT_PART_NAME）
    const char *actual_part = (part_name == NULL) ? NVS_DEFAULT_PART_NAME : part_name;
    // 调用原生接口打开命名空间
    esp_err_t ret = nvs_open_from_partition(actual_part, ns_name, open_mode, out_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Open namespace failed [part: %s, ns: %s, mode: %d]: 0x%x",
                 actual_part, ns_name, open_mode, ret);
    }
    return ret;
}

/**
 * @brief 辅助函数：关闭NVS命名空间（内部使用，封装原生nvs_close）
 * @param handle 要关闭的handle
 */
static void __nvs_custom_close(nvs_handle_t handle)
{
    if (handle != 0)
    { // 原生handle非0表示有效
        nvs_close(handle);
        ESP_LOGD(TAG, "Close NVS handle: %d", handle);
    }
}

// -------------------------- 无符号整数类型封装 --------------------------
esp_err_t nvs_custom_set_u8(const char *part_name, const char *ns_name, const char *key, uint8_t value)
{
    if (key == NULL)
    {
        ESP_LOGE(TAG, "Invalid param: key is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle;
    // 打开命名空间（读写模式）
    esp_err_t ret = __nvs_custom_open(part_name, ns_name, NVS_READWRITE, &handle);
    if (ret != ESP_OK)
    {
        return ret;
    }
    // 调用原生接口写入uint8_t
    ret = nvs_set_u8(handle, key, value);
    if (ret == ESP_OK)
    {
        // 提交写入（原生set操作需commit才会同步到Flash）
        ret = nvs_commit(handle);
        if (ret == ESP_OK)
        {
            ESP_LOGI(TAG, "Set u8 success [ns: %s, key: %s, value: %hhu]", ns_name, key, value);
        }
        else
        {
            ESP_LOGE(TAG, "Commit u8 failed [ns: %s, key: %s]: 0x%x", ns_name, key, ret);
        }
    }
    else
    {
        ESP_LOGE(TAG, "Set u8 failed [ns: %s, key: %s]: 0x%x", ns_name, key, ret);
    }
    // 无论成功与否，必须关闭handle（避免资源泄漏）
    __nvs_custom_close(handle);
    return ret;
}

esp_err_t nvs_custom_get_u8(const char *part_name, const char *ns_name, const char *key, uint8_t *out_value)
{
    if (key == NULL || out_value == NULL)
    {
        ESP_LOGE(TAG, "Invalid param: key or out_value is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle;
    // 打开命名空间（只读模式，更安全）
    esp_err_t ret = __nvs_custom_open(part_name, ns_name, NVS_READONLY, &handle);
    if (ret != ESP_OK)
    {
        return ret;
    }
    // 调用原生接口读取uint8_t
    ret = nvs_get_u8(handle, key, out_value);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Get u8 success [ns: %s, key: %s, value: %hhu]", ns_name, key, *out_value);
    }
    else if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "Get u8 failed: key not found [ns: %s, key: %s]", ns_name, key);
    }
    else
    {
        ESP_LOGE(TAG, "Get u8 failed [ns: %s, key: %s]: 0x%x", ns_name, key, ret);
    }
    // 关闭handle
    __nvs_custom_close(handle);
    return ret;
}

esp_err_t nvs_custom_set_u16(const char *part_name, const char *ns_name, const char *key, uint16_t value)
{
    if (key == NULL)
        return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t ret = __nvs_custom_open(part_name, ns_name, NVS_READWRITE, &handle);
    if (ret != ESP_OK)
        return ret;
    ret = nvs_set_u16(handle, key, value);
    if (ret == ESP_OK)
    {
        ret = nvs_commit(handle);
        if (ret == ESP_OK)
        {
            ESP_LOGI(TAG, "Set u16 success [ns: %s, key: %s, value: %hu]", ns_name, key, value);
        }
    }
    else
    {
        ESP_LOGE(TAG, "Set u16 failed [ns: %s, key: %s]: 0x%x", ns_name, key, ret);
    }
    __nvs_custom_close(handle);
    return ret;
}

esp_err_t nvs_custom_get_u16(const char *part_name, const char *ns_name, const char *key, uint16_t *out_value)
{
    if (key == NULL || out_value == NULL)
        return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t ret = __nvs_custom_open(part_name, ns_name, NVS_READONLY, &handle);
    if (ret != ESP_OK)
        return ret;
    ret = nvs_get_u16(handle, key, out_value);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Get u16 success [ns: %s, key: %s, value: %hu]", ns_name, key, *out_value);
    }
    else if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "Get u16 failed: key not found [ns: %s, key: %s]", ns_name, key);
    }
    else
    {
        ESP_LOGE(TAG, "Get u16 failed [ns: %s, key: %s]: 0x%x", ns_name, key, ret);
    }
    __nvs_custom_close(handle);
    return ret;
}

esp_err_t nvs_custom_set_u32(const char *part_name, const char *ns_name, const char *key, uint32_t value)
{
    if (key == NULL)
        return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t ret = __nvs_custom_open(part_name, ns_name, NVS_READWRITE, &handle);
    if (ret != ESP_OK)
        return ret;
    ret = nvs_set_u32(handle, key, value);
    if (ret == ESP_OK)
    {
        ret = nvs_commit(handle);
        if (ret == ESP_OK)
        {
            ESP_LOGI(TAG, "Set u32 success [ns: %s, key: %s, value: %u]", ns_name, key, value);
        }
    }
    else
    {
        ESP_LOGE(TAG, "Set u32 failed [ns: %s, key: %s]: 0x%x", ns_name, key, ret);
    }
    __nvs_custom_close(handle);
    return ret;
}

esp_err_t nvs_custom_get_u32(const char *part_name, const char *ns_name, const char *key, uint32_t *out_value)
{
    if (key == NULL || out_value == NULL)
        return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t ret = __nvs_custom_open(part_name, ns_name, NVS_READONLY, &handle);
    if (ret != ESP_OK)
        return ret;
    ret = nvs_get_u32(handle, key, out_value);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Get u32 success [ns: %s, key: %s, value: %u]", ns_name, key, *out_value);
    }
    else if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "Get u32 failed: key not found [ns: %s, key: %s]", ns_name, key);
    }
    else
    {
        ESP_LOGE(TAG, "Get u32 failed [ns: %s, key: %s]: 0x%x", ns_name, key, ret);
    }
    __nvs_custom_close(handle);
    return ret;
}

esp_err_t nvs_custom_set_u64(const char *part_name, const char *ns_name, const char *key, uint64_t value)
{
    if (key == NULL)
        return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t ret = __nvs_custom_open(part_name, ns_name, NVS_READWRITE, &handle);
    if (ret != ESP_OK)
        return ret;
    ret = nvs_set_u64(handle, key, value);
    if (ret == ESP_OK)
    {
        ret = nvs_commit(handle);
        if (ret == ESP_OK)
        {
            ESP_LOGI(TAG, "Set u64 success [ns: %s, key: %s, value: %llu]", ns_name, key, value);
        }
    }
    else
    {
        ESP_LOGE(TAG, "Set u64 failed [ns: %s, key: %s]: 0x%x", ns_name, key, ret);
    }
    __nvs_custom_close(handle);
    return ret;
}

esp_err_t nvs_custom_get_u64(const char *part_name, const char *ns_name, const char *key, uint64_t *out_value)
{
    if (key == NULL || out_value == NULL)
        return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t ret = __nvs_custom_open(part_name, ns_name, NVS_READONLY, &handle);
    if (ret != ESP_OK)
        return ret;
    ret = nvs_get_u64(handle, key, out_value);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Get u64 success [ns: %s, key: %s, value: %llu]", ns_name, key, *out_value);
    }
    else if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "Get u64 failed: key not found [ns: %s, key: %s]", ns_name, key);
    }
    else
    {
        ESP_LOGE(TAG, "Get u64 failed [ns: %s, key: %s]: 0x%x", ns_name, key, ret);
    }
    __nvs_custom_close(handle);
    return ret;
}

esp_err_t nvs_custom_set_i8(const char *part_name, const char *ns_name, const char *key, int8_t value)
{
    if (key == NULL)
        return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t ret = __nvs_custom_open(part_name, ns_name, NVS_READWRITE, &handle);
    if (ret != ESP_OK)
        return ret;
    ret = nvs_set_i8(handle, key, value);
    if (ret == ESP_OK)
    {
        ret = nvs_commit(handle);
        if (ret == ESP_OK)
        {
            ESP_LOGI(TAG, "Set i8 success [ns: %s, key: %s, value: %hhd]", ns_name, key, value);
        }
    }
    else
    {
        ESP_LOGE(TAG, "Set i8 failed [ns: %s, key: %s]: 0x%x", ns_name, key, ret);
    }
    __nvs_custom_close(handle);
    return ret;
}

esp_err_t nvs_custom_get_i8(const char *part_name, const char *ns_name, const char *key, int8_t *out_value)
{
    if (key == NULL || out_value == NULL)
        return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t ret = __nvs_custom_open(part_name, ns_name, NVS_READONLY, &handle);
    if (ret != ESP_OK)
        return ret;
    ret = nvs_get_i8(handle, key, out_value);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Get i8 success [ns: %s, key: %s, value: %hhd]", ns_name, key, *out_value);
    }
    else if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "Get i8 failed: key not found [ns: %s, key: %s]", ns_name, key);
    }
    else
    {
        ESP_LOGE(TAG, "Get i8 failed [ns: %s, key: %s]: 0x%x", ns_name, key, ret);
    }
    __nvs_custom_close(handle);
    return ret;
}

esp_err_t nvs_custom_set_i16(const char *part_name, const char *ns_name, const char *key, int16_t value)
{
    if (key == NULL)
        return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t ret = __nvs_custom_open(part_name, ns_name, NVS_READWRITE, &handle);
    if (ret != ESP_OK)
        return ret;
    ret = nvs_set_i16(handle, key, value);
    if (ret == ESP_OK)
    {
        ret = nvs_commit(handle);
        if (ret == ESP_OK)
        {
            ESP_LOGI(TAG, "Set i16 success [ns: %s, key: %s, value: %hd]", ns_name, key, value);
        }
    }
    else
    {
        ESP_LOGE(TAG, "Set i16 failed [ns: %s, key: %s]: 0x%x", ns_name, key, ret);
    }
    __nvs_custom_close(handle);
    return ret;
}

esp_err_t nvs_custom_get_i16(const char *part_name, const char *ns_name, const char *key, int16_t *out_value)
{
    if (key == NULL || out_value == NULL)
        return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t ret = __nvs_custom_open(part_name, ns_name, NVS_READONLY, &handle);
    if (ret != ESP_OK)
        return ret;
    ret = nvs_get_i16(handle, key, out_value);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Get i16 success [ns: %s, key: %s, value: %hd]", ns_name, key, *out_value);
    }
    else if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "Get i16 failed: key not found [ns: %s, key: %s]", ns_name, key);
    }
    else
    {
        ESP_LOGE(TAG, "Get i16 failed [ns: %s, key: %s]: 0x%x", ns_name, key, ret);
    }
    __nvs_custom_close(handle);
    return ret;
}

esp_err_t nvs_custom_set_i32(const char *part_name, const char *ns_name, const char *key, int32_t value)
{
    if (key == NULL)
        return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t ret = __nvs_custom_open(part_name, ns_name, NVS_READWRITE, &handle);
    if (ret != ESP_OK)
        return ret;
    ret = nvs_set_i32(handle, key, value);
    if (ret == ESP_OK)
    {
        ret = nvs_commit(handle);
        if (ret == ESP_OK)
        {
            ESP_LOGI(TAG, "Set i32 success [ns: %s, key: %s, value: %d]", ns_name, key, value);
        }
    }
    else
    {
        ESP_LOGE(TAG, "Set i32 failed [ns: %s, key: %s]: 0x%x", ns_name, key, ret);
    }
    __nvs_custom_close(handle);
    return ret;
}

esp_err_t nvs_custom_get_i32(const char *part_name, const char *ns_name, const char *key, int32_t *out_value)
{
    if (key == NULL || out_value == NULL)
        return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t ret = __nvs_custom_open(part_name, ns_name, NVS_READONLY, &handle);
    if (ret != ESP_OK)
        return ret;
    ret = nvs_get_i32(handle, key, out_value);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Get i32 success [ns: %s, key: %s, value: %d]", ns_name, key, *out_value);
    }
    else if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "Get i32 failed: key not found [ns: %s, key: %s]", ns_name, key);
    }
    else
    {
        ESP_LOGE(TAG, "Get i32 failed [ns: %s, key: %s]: 0x%x", ns_name, key, ret);
    }
    __nvs_custom_close(handle);
    return ret;
}

esp_err_t nvs_custom_set_i64(const char *part_name, const char *ns_name, const char *key, int64_t value)
{
    if (key == NULL)
        return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t ret = __nvs_custom_open(part_name, ns_name, NVS_READWRITE, &handle);
    if (ret != ESP_OK)
        return ret;
    ret = nvs_set_i64(handle, key, value);
    if (ret == ESP_OK)
    {
        ret = nvs_commit(handle);
        if (ret == ESP_OK)
        {
            ESP_LOGI(TAG, "Set i64 success [ns: %s, key: %s, value: %lld]", ns_name, key, value);
        }
    }
    else
    {
        ESP_LOGE(TAG, "Set i64 failed [ns: %s, key: %s]: 0x%x", ns_name, key, ret);
    }
    __nvs_custom_close(handle);
    return ret;
}

esp_err_t nvs_custom_get_i64(const char *part_name, const char *ns_name, const char *key, int64_t *out_value)
{
    if (key == NULL || out_value == NULL)
        return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t ret = __nvs_custom_open(part_name, ns_name, NVS_READONLY, &handle);
    if (ret != ESP_OK)
        return ret;
    ret = nvs_get_i64(handle, key, out_value);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Get i64 success [ns: %s, key: %s, value: %lld]", ns_name, key, *out_value);
    }
    else if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "Get i64 failed: key not found [ns: %s, key: %s]", ns_name, key);
    }
    else
    {
        ESP_LOGE(TAG, "Get i64 failed [ns: %s, key: %s]: 0x%x", ns_name, key, ret);
    }
    __nvs_custom_close(handle);
    return ret;
}

// -------------------------- 字符串类型封装 --------------------------
esp_err_t nvs_custom_set_str(const char *part_name, const char *ns_name, const char *key, const char *value)
{
    if (key == NULL || value == NULL)
    {
        ESP_LOGE(TAG, "Invalid param: key or value is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle;
    esp_err_t ret = __nvs_custom_open(part_name, ns_name, NVS_READWRITE, &handle);
    if (ret != ESP_OK)
        return ret;
    // 调用原生接口写入字符串
    ret = nvs_set_str(handle, key, value);
    if (ret == ESP_OK)
    {
        ret = nvs_commit(handle);
        if (ret == ESP_OK)
        {
            ESP_LOGI(TAG, "Set str success [ns: %s, key: %s, value: %s]", ns_name, key, value);
        }
    }
    else
    {
        ESP_LOGE(TAG, "Set str failed [ns: %s, key: %s]: 0x%x", ns_name, key, ret);
    }
    __nvs_custom_close(handle);
    return ret;
}

esp_err_t nvs_custom_get_str(const char *part_name, const char *ns_name, const char *key, char *out_buf, size_t *buf_len)
{
    if (key == NULL || out_buf == NULL || buf_len == NULL)
    {
        ESP_LOGE(TAG, "Invalid param: key/out_buf/buf_len is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle;
    esp_err_t ret = __nvs_custom_open(part_name, ns_name, NVS_READONLY, &handle);
    if (ret != ESP_OK)
        return ret;
    // 调用原生接口读取字符串（需传入缓冲区和长度指针）
    ret = nvs_get_str(handle, key, out_buf, buf_len);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Get str success [ns: %s, key: %s, value: %s, len: %zu]",
                 ns_name, key, out_buf, *buf_len);
    }
    else if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "Get str failed: key not found [ns: %s, key: %s]", ns_name, key);
    }
    else if (ret == ESP_ERR_NVS_INVALID_LENGTH)
    {
        ESP_LOGE(TAG, "Get str failed: buffer too small [need: %zu, current: %zu]",
                 *buf_len, *buf_len); // 原生会将需要的长度写入buf_len
    }
    else
    {
        ESP_LOGE(TAG, "Get str failed [ns: %s, key: %s]: 0x%x", ns_name, key, ret);
    }
    __nvs_custom_close(handle);
    return ret;
}

// -------------------------- 数组类型封装 --------------------------
esp_err_t nvs_custom_set_blob(const char *part_name, const char *ns_name, const char *key, const void *value, size_t value_size)
{
    if (key == NULL || value == NULL || value_size == 0)
    {
        ESP_LOGE(TAG, "Invalid param: key/value is NULL or value_size is 0");
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle;
    esp_err_t ret = __nvs_custom_open(part_name, ns_name, NVS_READWRITE, &handle);
    if (ret != ESP_OK)
        return ret;
    // 调用原生接口写入blob
    ret = nvs_set_blob(handle, key, value, value_size);
    if (ret == ESP_OK)
    {
        ret = nvs_commit(handle);
        if (ret == ESP_OK)
        {
            ESP_LOGI(TAG, "Set blob success [ns: %s, key: %s, size: %zu]", ns_name, key, value_size);
        }
    }
    else
    {
        ESP_LOGE(TAG, "Set blob failed [ns: %s, key: %s]: 0x%x", ns_name, key, ret);
    }
    __nvs_custom_close(handle);
    return ret;
}

esp_err_t nvs_custom_get_blob(const char *part_name, const char *ns_name, const char *key, void *out_buf, size_t *buf_size)
{
    if (key == NULL || out_buf == NULL || buf_size == NULL || *buf_size == 0)
    {
        ESP_LOGE(TAG, "Invalid param: key/out_buf/buf_size is NULL or *buf_size is 0");
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle;
    esp_err_t ret = __nvs_custom_open(part_name, ns_name, NVS_READONLY, &handle);
    if (ret != ESP_OK)
        return ret;
    // 调用原生接口读取blob
    ret = nvs_get_blob(handle, key, out_buf, buf_size);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Get blob success [ns: %s, key: %s, size: %zu]", ns_name, key, *buf_size);
    }
    else if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "Get blob failed: key not found [ns: %s, key: %s]", ns_name, key);
    }
    else if (ret == ESP_ERR_NVS_INVALID_LENGTH)
    {
        ESP_LOGE(TAG, "Get blob failed: buffer too small [need: %zu, current: %zu]",
                 *buf_size, *buf_size); // 原生会将需要的长度写入buf_size
    }
    else
    {
        ESP_LOGE(TAG, "Get blob failed [ns: %s, key: %s]: 0x%x", ns_name, key, ret);
    }
    __nvs_custom_close(handle);
    return ret;
}

// -------------------------- 删除操作封装 --------------------------
esp_err_t nvs_custom_erase_key(const char *part_name, const char *ns_name, const char *key)
{
    if (key == NULL)
    {
        ESP_LOGE(TAG, "Invalid param: key is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle;
    esp_err_t ret = __nvs_custom_open(part_name, ns_name, NVS_READWRITE, &handle);
    if (ret != ESP_OK)
        return ret;
    // 调用原生接口删除指定键
    ret = nvs_erase_key(handle, key);
    if (ret == ESP_OK)
    {
        ret = nvs_commit(handle);
        if (ret == ESP_OK)
        {
            ESP_LOGI(TAG, "Erase key success [ns: %s, key: %s]", ns_name, key);
        }
    }
    else
    {
        ESP_LOGE(TAG, "Erase key failed [ns: %s, key: %s]: 0x%x", ns_name, key, ret);
    }
    __nvs_custom_close(handle);
    return ret;
}

esp_err_t nvs_custom_erase_all(const char *part_name, const char *ns_name)
{
    nvs_handle_t handle;
    esp_err_t ret = __nvs_custom_open(part_name, ns_name, NVS_READWRITE, &handle);
    if (ret != ESP_OK)
        return ret;
    // 调用原生接口删除命名空间所有键
    ret = nvs_erase_all(handle);
    if (ret == ESP_OK)
    {
        ret = nvs_commit(handle);
        if (ret == ESP_OK)
        {
            ESP_LOGI(TAG, "Erase all success [ns: %s]", ns_name);
        }
    }
    else
    {
        ESP_LOGE(TAG, "Erase all failed [ns: %s]: 0x%x", ns_name, ret);
    }
    __nvs_custom_close(handle);
    return ret;
}
