/*
 * zone_manager_host.h - Wrapper cho host test (máy tính, GCC).
 *
 * Thêm các define cần thiết không có trên host, sau đó include
 * zone_manager.h và khai báo ErrorFromWindow (exposed để test).
 *
 * KHÔNG dùng file này trong firmware.
 */

#ifndef ZONE_MANAGER_HOST_H
#define ZONE_MANAGER_HOST_H

#include <stdint.h>

/* Fake BOARD_CONTROL_PERIOD_MS nếu chưa defined */
#ifndef BOARD_CONTROL_PERIOD_MS
#define BOARD_CONTROL_PERIOD_MS 2U
#endif

/* Fake ENABLE_MAP1_OPT nếu chưa defined */
#ifndef ENABLE_MAP1_OPT
#define ENABLE_MAP1_OPT 1
#endif

/* Fake ZM_MAP1_SANITIZE_MASK nếu chưa defined */
#ifndef ZM_MAP1_SANITIZE_MASK
#define ZM_MAP1_SANITIZE_MASK 1
#endif

/* Include các struct/enum/API chuẩn từ zone_manager.h */
#include "../../app/zone_manager.h"

/*
 * ErrorFromWindow được khai báo non-static để test file có thể gọi trực tiếp.
 * (Trong zone_manager.c trên firmware, hàm này được khai báo non-static
 *  để hỗ trợ test host - đây là ngoại lệ có chủ ý.)
 */
int16_t ErrorFromWindow(uint8_t window);

#endif /* ZONE_MANAGER_HOST_H */

