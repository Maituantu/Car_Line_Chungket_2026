/*
 * zone_manager.h - Zone Manager B46: Bám cụm/cửa sổ line thật cho Map 1.
 *
 * PHẠM VI: Chỉ xử lý đoạn đầu Map 1 (cung nhiễu 1 -> ngã 3 vòng tròn).
 * Sau khi xử lý xong các ngã rẽ (ZM_MAP1_FORK_COUNT), trả quyền cho code cũ.
 */

#ifndef ZONE_MANAGER_H
#define ZONE_MANAGER_H

#include <stdint.h>

#ifndef BOARD_CONTROL_PERIOD_MS
#define BOARD_CONTROL_PERIOD_MS 2U
#endif

/* ============================================================
 * CẤU HÌNH BIÊN DỊCH
 * ============================================================ */

/* ENABLE_MAP1_OPT: 0 = tắt, 1 = bật thật, 2 = shadow mode */
#ifndef ENABLE_MAP1_OPT
#define ENABLE_MAP1_OPT 1
#endif

/* Trạng thái máy trạng thái chính */
#define ZM_STATE_IDLE       (0U)
#define ZM_STATE_M1_CIRCLE  (1U)

/* 3) Pha */
#define ZM_PHASE_ARMING     (0U)
#define ZM_PHASE_WAIT_FORK  (1U)
#define ZM_PHASE_IN_FORK    (2U)

/* 4) Luật chọn nhánh tại mỗi ngã rẽ */
#define ZM_FORK_AWAY        (0U)  /* đi nhánh XA phía lệch của pattern rộng (bỏ cung nhiễu, đi thẳng) */
#define ZM_FORK_TOWARD      (1U)  /* đi nhánh VỀ phía lệch */
#define ZM_FORK_OPP_FIRST   (2U)  /* đi NGƯỢC phía lệch của ngã rẽ đầu (ngã 3 vòng tròn) */
#define ZM_FORK_LEFT        (3U)  /* luôn nhánh trái (canonical) */
#define ZM_FORK_RIGHT       (4U)  /* luôn nhánh phải (canonical) */

#define ZM_MAP1_FORK_COUNT        (2U)               /* số ngã rẽ cần xử lý: 1 = cung nhiễu, 2 = ngã 3 */
#define ZM_MAP1_FORK1_RULE        (ZM_FORK_AWAY)     /* cung nhiễu: bám line thật, không rẽ theo cung */
#define ZM_MAP1_FORK2_RULE        (ZM_FORK_OPP_FIRST)/* ngã 3: ngược phía cung nhiễu. Sai hướng thì đổi thành ZM_FORK_TOWARD */
#define ZM_MAP1_FORK_CLUSTER_FROM (1U)               /* từ ngã rẽ thứ mấy (0-based) thì 2 cụm tách nhau cũng tính là ngã rẽ */

/* 5) Thời gian (ms) */
#define ZM_MAP1_ARM_NARROW_MS         (20U)     /* sau ô START, 20ms line hẹp là sẵn sàng */
#define ZM_MAP1_ARM_MAX_MS            (1500U)
#define ZM_MAP1_FORK_WAIT_MAX_MS      (2500U)   /* chờ ngã rẽ kế tiếp tối đa */
#define ZM_MAP1_FORK_MAX_MS           (1500U)   /* một ngã rẽ kéo dài tối đa */
#define ZM_MAP1_FORK_DEBOUNCE_MS      (16U)     /* debounce ngã rẽ 0 (cung nhiễu) */
#define ZM_MAP1_FORK1_DEBOUNCE_MS     (6U)      /* debounce ngã rẽ 1 (ngã 3 vòng tròn: bẻ sớm) */
#define ZM_MAP1_FORK_EXIT_DEBOUNCE_MS (16U)     /* thoát fork 0 nhanh: đoạn thẳng chỉ 3-5cm */
#define ZM_MAP1_BRANCH_HOLD_MS        (80U)     /* không thấy nhánh: giữ error cuối tối đa */
#define ZM_MAP1_TRACK_LOST_MS         (80U)     /* mất bám liên tục -> Handover */
#define ZM_MAP1_MAX_STEER_IN_RING     (2200)    /* giới hạn error góc lái tối đa */

/* 6) Cửa sổ line và error – PHẢI chỉnh theo estimator thật */
#define ZM_MAP1_LINE_WIDTH_EYES   (2U)    /* độ rộng line theo số mắt */
#define ZM_MAP1_TRACK_MAX_JUMP    (5U)    /* dịch chuyển tối đa/frame, đơn vị nửa mắt (5 = 2.5 mắt) */
#define ZM_MAP1_ERR_PER_HALF_EYE  (500)   /* error ứng với 1 nửa mắt: 1 mắt = 1000 -> 1/2 mắt = 500 */
#define ZM_MAP1_ERR_SIGN          (1)     /* +1: error dương = line lệch PHẢI; -1 nếu ngược */
#define ZM_MAP1_ERR_SLEW_PER_FRAME (250)  /* bước đổi error/frame: bẻ lái nhanh vào ngã 3 */

/* 7) Mask đưa cho Control_Step khi đang bám (bit3, bit4) */
#define ZM_MAP1_PASS_MASK        (0x18U)
#define ZM_MAP1_SANITIZE_MASK    (1U)
#define ZM_MAP1_NARROW_MAX_EYES  (3U)

/* 8) Kiểm tra lúc biên dịch */
#if (ZM_MAP1_FORK_DEBOUNCE_MS > 255)
#error "ZM_MAP1_FORK_DEBOUNCE_MS phai <= 255 (arc_candidate_ms la uint8_t)"
#endif
#ifndef ZM_MAP1_NARROW_MAX_EYES
#error "Thieu ZM_MAP1_NARROW_MAX_EYES"
#endif

/* ============================================================
 * CẤU TRÚC DỮ LIỆU
 * ============================================================ */

/* Hành động ZoneManager trả về cho robot.c */
typedef enum {
    ZM_ACTION_NONE           = 0, /* Không can thiệp, dùng error/mask gốc */
    ZM_ACTION_CLAMP_ERROR    = 1, /* Ghi đè control_error */
    ZM_ACTION_FORCE_STRAIGHT = 2  /* Ghi đè control_error + ép control_valid = 1 */
} ZoneManagerAction;

/* Dữ liệu đầu vào mỗi frame từ robot.c */
typedef struct {
    uint32_t timestamp_ms;
    uint8_t  canonical_mask;
    uint8_t  valid;
    int16_t  error;
    uint8_t  eyes_left;
    uint8_t  eyes_right;
} ZoneManagerInput;

/* 2) struct ZoneManagerOutput: thêm trường uint8_t mask; */
typedef struct {
    ZoneManagerAction action;
    int16_t           error;
    uint8_t           mask;     /* Mask cửa sổ đưa cho Control_Step */
} ZoneManagerOutput;

/* 1) struct ZoneManager: thêm các trường */
typedef struct {
    /* Trường gốc */
    uint8_t  state;             /* ZM_STATE_IDLE, ZM_STATE_M1_CIRCLE */
    int8_t   arc_dir;           /* phía lệch của ngã rẽ đầu: -1 trái, +1 phải */
    uint16_t arc_seen_ms;
    uint16_t arc_missing_ms;
    uint16_t narrow_ms;
    uint16_t blind_ms;
    int16_t  arc_bias;
    int8_t   arc_candidate_dir;
    uint8_t  arc_candidate_ms;

    /* Các trường mới theo yêu cầu: */
    uint8_t  phase;             /* ZM_PHASE_ARMING, ZM_PHASE_WAIT_FORK, ZM_PHASE_IN_FORK */
    uint8_t  fork_idx;          /* 0 = cung nhiễu 1, 1 = ngã 3 */
    uint8_t  locked;            /* đã khóa nhánh tại ngã rẽ chưa */
    uint8_t  last_mask;         /* mask đã bám gần nhất */
    int8_t   heavy_side;        /* phía lệch của pattern rộng (-1 trái, +1 phải) */
    int8_t   target_side;       /* phía nhánh mục tiêu cần đi (-1 trái, +1 phải) */
    int8_t   last_center2;      /* tâm cửa sổ bám gần nhất (nửa mắt, first + last) */
    int16_t  last_error;        /* error hợp lệ cuối cùng */
    int16_t  side_score;        /* tích lũy độ lệch trái/phải để chống nhiễu */

    /* Bộ đếm thời gian chuyển pha và an toàn */
    uint16_t arm_ms;
    uint16_t wait_fork_ms;
    uint16_t fork_ms;
    uint16_t exit_debounce_ms;
    uint16_t track_lost_ms;
} ZoneManager;

/* ============================================================
 * API CÔNG KHAI
 * ============================================================ */
void ZM_Init(ZoneManager *zm);
void ZM_EnterMap1Circle(ZoneManager *zm);
void ZM_Step(ZoneManager *zm,
             const ZoneManagerInput *input,
             ZoneManagerOutput *output);

/* Helper tính error từ window (hỗ trợ test host) */
int16_t ErrorFromWindow(uint8_t window);

#endif /* ZONE_MANAGER_H */
