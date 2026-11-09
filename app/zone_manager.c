/*
 * zone_manager.c - Zone Manager B46 (Mô hình Fork 2 ngã rẽ: cung nhiễu 1 -> ngã 3).
 *
 * PHẠM VI: Chỉ xử lý đoạn đầu Map 1, không can thiệp sau Handover.
 */

#include "zone_manager.h"
#include "board.h"
#include "timebase.h"

/* ============================================================
 * CÁC HÀM TIỆN ÍCH NỘI BỘ (STATIC, LOGIC THUẦN)
 * ============================================================ */

static uint8_t CountBits(uint8_t mask)
{
    uint8_t count = 0;
    while (mask) { count += mask & 1U; mask >>= 1; }
    return count;
}

/* Một pattern là ngã rẽ khi:
 * 1. Pattern rộng (số mắt bật > ZM_MAP1_NARROW_MAX_EYES), HOẶC
 * 2. Có từ 2 cụm tách nhau trở lên (FindClusters >= 2) - ví dụ ngã 3 tách 2 nhánh */
static uint8_t IsForkPattern(uint8_t bits_cnt, uint8_t n_clusters, uint8_t fork_idx)
{
    if (bits_cnt > ZM_MAP1_NARROW_MAX_EYES) return 1U;
    if (fork_idx >= ZM_MAP1_FORK_CLUSTER_FROM && n_clusters >= 2U) return 1U;
    return 0U;
}

static uint8_t FindClusters(uint8_t mask,
                            uint8_t *first_out,
                            uint8_t *last_out,
                            uint8_t max_clusters)
{
    uint8_t count = 0;
    uint8_t i;
    uint8_t in_cluster = 0;

    for (i = 0; i < 8U; ++i) {
        if (mask & (1U << i)) {
            if (!in_cluster) {
                if (count < max_clusters) {
                    first_out[count] = i;
                    last_out[count]  = i;
                }
                in_cluster = 1;
            } else {
                if (count < max_clusters) last_out[count] = i;
            }
        } else {
            if (in_cluster) {
                ++count;
                in_cluster = 0;
            }
        }
    }
    if (in_cluster) ++count;
    return count;
}

static uint8_t PickWindow(uint8_t mask,
                          int16_t ref_center2,
                          uint8_t width_eyes,
                          uint8_t *out_start,
                          uint8_t *out_end,
                          int16_t *out_center2)
{
    uint8_t best_start = 0, best_end = 0;
    int16_t best_dist  = 32767;
    int16_t best_c2    = 0;
    uint8_t found      = 0;
    uint8_t start;

    if (width_eyes == 0U || width_eyes > 8U || mask == 0U) return 0;

    /* 1. Quét tìm cửa sổ width_eyes mắt liên tiếp */
    for (start = 0; start + width_eyes <= 8U; ++start) {
        uint8_t end = (uint8_t)(start + width_eyes - 1U);
        uint8_t w;
        int16_t c2, dist;
        uint8_t all_set = 1U;

        for (w = start; w <= end; ++w) {
            if (!(mask & (1U << w))) { all_set = 0; break; }
        }
        if (!all_set) continue;

        c2   = (int16_t)(start + end);
        dist = (int16_t)(c2 - ref_center2);
        if (dist < 0) dist = (int16_t)-dist;

        if (!found || dist < best_dist) {
            best_dist   = dist;
            best_start  = start;
            best_end    = end;
            best_c2     = c2;
            found       = 1U;
        }
    }

    /* 2. Fallback: nếu line mỏng (chỉ 1 mắt bật), tìm 1 mắt gần ref_center2 nhất */
    if (!found && width_eyes > 1U) {
        for (start = 0; start < 8U; ++start) {
            if (mask & (1U << start)) {
                int16_t c2   = (int16_t)(start + start);
                int16_t dist = (int16_t)(c2 - ref_center2);
                if (dist < 0) dist = (int16_t)-dist;
                if (!found || dist < best_dist) {
                    best_dist   = dist;
                    best_start  = start;
                    best_end    = start;
                    best_c2     = c2;
                    found       = 1U;
                }
            }
        }
    }

    if (!found) return 0;

    *out_start   = best_start;
    *out_end     = best_end;
    *out_center2 = best_c2;

    return (best_dist <= (int16_t)ZM_MAP1_TRACK_MAX_JUMP) ? 1U : 0U;
}

static uint8_t PickWindowAtSide(uint8_t mask,
                                int8_t side,
                                uint8_t width_eyes,
                                uint8_t *out_start,
                                uint8_t *out_end,
                                int16_t *out_center2)
{
    uint8_t start;

    if (width_eyes == 0U || width_eyes > 8U || mask == 0U) return 0;

    if (side <= 0) {
        /* Nhánh TRÁI: ưu tiên quét từ bit 0 lên cho cửa sổ width_eyes */
        for (start = 0; start + width_eyes <= 8U; ++start) {
            uint8_t end = (uint8_t)(start + width_eyes - 1U);
            uint8_t w;
            uint8_t all_set = 1U;
            for (w = start; w <= end; ++w) {
                if (!(mask & (1U << w))) { all_set = 0; break; }
            }
            if (all_set) {
                *out_start   = start;
                *out_end     = end;
                *out_center2 = (int16_t)(start + end);
                return 1U;
            }
        }
        /* Fallback 1 mắt bên trái nếu line mỏng ở mép */
        for (start = 0; start < 8U; ++start) {
            if (mask & (1U << start)) {
                *out_start   = start;
                *out_end     = start;
                *out_center2 = (int16_t)(start + start);
                return 1U;
            }
        }
    } else {
        /* Nhánh PHẢI: ưu tiên quét từ bit 7 xuống cho cửa sổ width_eyes */
        int8_t s;
        for (s = (int8_t)(8 - (int8_t)width_eyes); s >= 0; --s) {
            uint8_t st  = (uint8_t)s;
            uint8_t end = (uint8_t)(st + width_eyes - 1U);
            uint8_t w;
            uint8_t all_set = 1U;
            for (w = st; w <= end; ++w) {
                if (!(mask & (1U << w))) { all_set = 0; break; }
            }
            if (all_set) {
                *out_start   = st;
                *out_end     = end;
                *out_center2 = (int16_t)(st + end);
                return 1U;
            }
        }
        /* Fallback 1 mắt bên phải nếu line mỏng ở mép */
        for (s = 7; s >= 0; --s) {
            if (mask & (1U << s)) {
                *out_start   = (uint8_t)s;
                *out_end     = (uint8_t)s;
                *out_center2 = (int16_t)(s + s);
                return 1U;
            }
        }
    }
    return 0U;
}

int16_t ErrorFromWindow(uint8_t window)
{
    uint8_t first = 0, last = 0, i;
    int32_t c2;

    if (window == 0U) return 0;

    for (i = 0; i < 8U; ++i) {
        if (window & (1U << i)) { first = i; break; }
    }
    last = first;
    for (i = first + 1U; i < 8U; ++i) {
        if (window & (1U << i)) last = i;
    }

    c2 = (int32_t)first + (int32_t)last;
    return (int16_t)((c2 - 7) * ZM_MAP1_ERR_PER_HALF_EYE * ZM_MAP1_ERR_SIGN);
}

static int16_t SlewLimit(int16_t current, int16_t target, int16_t max_step)
{
    int16_t diff = (int16_t)(target - current);
    if (diff >  max_step) return (int16_t)(current + max_step);
    if (diff < -max_step) return (int16_t)(current - max_step);
    return target;
}

static void DoHandover(ZoneManager *zm)
{
    zm->state   = ZM_STATE_IDLE;
    zm->locked  = 0;
    zm->phase   = ZM_PHASE_WAIT_FORK;
}

/* ============================================================
 * CÁC HÀM KHỞI TẠO & ĐIỀU KHIỂN
 * ============================================================ */

void ZM_Init(ZoneManager *zm)
{
    static const ZoneManager zero = {0};
    *zm = zero;
    zm->state        = ZM_STATE_IDLE;
    zm->phase        = ZM_PHASE_ARMING;
    zm->last_center2 = 7;
    zm->last_mask    = ZM_MAP1_PASS_MASK;
}

void ZM_EnterMap1Circle(ZoneManager *zm)
{
    static const ZoneManager zero = {0};
    *zm = zero;
    zm->state        = ZM_STATE_M1_CIRCLE;
    zm->phase        = ZM_PHASE_ARMING;
    zm->fork_idx     = 0;
    zm->last_center2 = 7;
    zm->last_mask    = ZM_MAP1_PASS_MASK;
    zm->last_error   = 0;
    zm->locked       = 0;
}

void ZM_Step(ZoneManager *zm,
             const ZoneManagerInput *input,
             ZoneManagerOutput *output)
{
    uint8_t  mask    = input->canonical_mask;
    uint8_t  valid   = input->valid;
    int16_t  in_err  = input->error;
    uint32_t dt_ms   = BOARD_CONTROL_PERIOD_MS;
    uint8_t  bits_cnt = CountBits(mask);
    uint8_t  is_wide = bits_cnt > ZM_MAP1_NARROW_MAX_EYES;
    uint8_t  f_arr[8], l_arr[8];
    uint8_t  n_clusters = FindClusters(mask, f_arr, l_arr, 8U);

    output->action = ZM_ACTION_NONE;
    output->error  = in_err;
    output->mask   = mask;

    if (zm->state != ZM_STATE_M1_CIRCLE) return;

    switch (zm->phase) {

    /* ---------------------------------------------------------
     * PHA 0: ZM_PHASE_ARMING
     * Chờ line hẹp ổn định sau ô START.
     * --------------------------------------------------------- */
    case ZM_PHASE_ARMING:
    {
        zm->arm_ms = (uint16_t)(zm->arm_ms + (uint16_t)dt_ms);

        if (!is_wide && valid) {
            zm->narrow_ms = (uint16_t)(zm->narrow_ms + (uint16_t)dt_ms);
            if (n_clusters == 1U) {
                zm->last_center2 = (int8_t)(f_arr[0] + l_arr[0]);
                zm->last_error   = in_err;
            }
            if (zm->narrow_ms >= ZM_MAP1_ARM_NARROW_MS) {
                /* Đã armed -> chuyển sang chờ ngã rẽ đầu tiên */
                zm->phase            = ZM_PHASE_WAIT_FORK;
                zm->fork_idx         = 0;
                zm->wait_fork_ms     = 0;
                zm->arc_candidate_ms = 0;
                zm->narrow_ms        = 0;
                zm->locked           = 0;
            }
        } else {
            zm->narrow_ms = 0;
        }

        if (zm->arm_ms >= ZM_MAP1_ARM_MAX_MS) {
            DoHandover(zm);
        }
        output->action = ZM_ACTION_NONE;
        break;
    }

    /* ---------------------------------------------------------
     * PHA 1: ZM_PHASE_WAIT_FORK
     * Chờ phát hiện ngã rẽ thứ fork_idx.
     * --------------------------------------------------------- */
    case ZM_PHASE_WAIT_FORK:
    {
        uint8_t is_fork_cue = IsForkPattern(bits_cnt, n_clusters, zm->fork_idx);
        uint8_t target_debounce = (zm->fork_idx == 0U) ?
                                  (uint8_t)ZM_MAP1_FORK_DEBOUNCE_MS :
                                  (uint8_t)ZM_MAP1_FORK1_DEBOUNCE_MS;

        zm->wait_fork_ms = (uint16_t)(zm->wait_fork_ms + (uint16_t)dt_ms);

        if (is_fork_cue) {
            if (zm->arc_candidate_ms < 255U - (uint8_t)dt_ms) {
                zm->arc_candidate_ms = (uint8_t)(zm->arc_candidate_ms + (uint8_t)dt_ms);
            }

            /* Tích lũy độ lệch trái/phải trong suốt debounce để chống nhiễu */
            zm->side_score = (int16_t)(zm->side_score +
                                      (int16_t)input->eyes_right -
                                      (int16_t)input->eyes_left);

            if (zm->arc_candidate_ms >= target_debounce) {
                /* Xác nhận vào ngã rẽ! */
                uint8_t rule;
                zm->phase            = ZM_PHASE_IN_FORK;
                zm->fork_ms          = 0;
                zm->locked           = 0;
                zm->exit_debounce_ms = 0;
                zm->track_lost_ms    = 0;

                /* Xác định heavy_side từ tổng tích lũy */
                if (zm->side_score > 0) zm->heavy_side = 1;
                else if (zm->side_score < 0) zm->heavy_side = -1;
                else zm->heavy_side = (zm->last_center2 > 7) ? 1 : -1;

                /* Ghi nhớ arc_dir ở ngã rẽ đầu tiên (cung nhiễu 1) */
                if (zm->fork_idx == 0U) {
                    zm->arc_dir = zm->heavy_side;
                }

                /* Xác định target_side theo luật */
                rule = (zm->fork_idx == 0U) ? ZM_MAP1_FORK1_RULE : ZM_MAP1_FORK2_RULE;
                if (rule == ZM_FORK_AWAY) {
                    zm->target_side = (int8_t)(-zm->heavy_side);
                } else if (rule == ZM_FORK_TOWARD) {
                    zm->target_side = zm->heavy_side;
                } else if (rule == ZM_FORK_OPP_FIRST) {
                    zm->target_side = (int8_t)(-zm->arc_dir);
                } else if (rule == ZM_FORK_LEFT) {
                    zm->target_side = -1;
                } else if (rule == ZM_FORK_RIGHT) {
                    zm->target_side = 1;
                }
            }
        } else {
            zm->arc_candidate_ms = 0;
            zm->side_score       = 0;
            /* Cập nhật tâm bám khi chạy line hẹp */
            if (!is_wide && valid && n_clusters == 1U) {
                zm->last_center2 = (int8_t)(f_arr[0] + l_arr[0]);
                zm->last_error   = in_err;
            }
        }

        if (zm->wait_fork_ms >= ZM_MAP1_FORK_WAIT_MAX_MS) {
            DoHandover(zm);
        }
        output->action = ZM_ACTION_NONE;
        break;
    }

    /* ---------------------------------------------------------
     * PHA 2: ZM_PHASE_IN_FORK
     * Đang bám nhánh trong ngã rẽ thứ fork_idx.
     * --------------------------------------------------------- */
    case ZM_PHASE_IN_FORK:
    {
        uint8_t out_start = 0, out_end = 0;
        int16_t out_c2 = 0;
        uint8_t found = 0;
        int16_t computed_err, clamped_err;
        uint8_t is_fork = IsForkPattern(bits_cnt, n_clusters, zm->fork_idx);
        uint8_t is_narrow_exit;

        zm->fork_ms = (uint16_t)(zm->fork_ms + (uint16_t)dt_ms);

        if (!zm->locked) {
            /* Chưa khóa nhánh: chọn cửa sổ sát mép target_side */
            found = PickWindowAtSide(mask, zm->target_side,
                                     ZM_MAP1_LINE_WIDTH_EYES,
                                     &out_start, &out_end, &out_c2);
            if (found) {
                zm->last_center2  = (int8_t)out_c2;
                zm->locked        = 1U;
                zm->track_lost_ms = 0;
                /* Tại ngã 3 (fork 1): gán ngay bước lái ban đầu để xe bắt đầu bẻ lái ngay lập tức */
                if (zm->fork_idx >= 1U && zm->last_error == 0) {
                    zm->last_error = (int16_t)(zm->target_side * 1000 * ZM_MAP1_ERR_SIGN);
                }
            } else {
                zm->track_lost_ms = (uint16_t)(zm->track_lost_ms + (uint16_t)dt_ms);
                if (zm->track_lost_ms >= ZM_MAP1_BRANCH_HOLD_MS) {
                    DoHandover(zm); return;
                }
            }
        } else {
            /* Đã khóa nhánh: bám tiếp theo last_center2 (chống nhảy nhánh) */
            found = PickWindow(mask, zm->last_center2,
                               ZM_MAP1_LINE_WIDTH_EYES,
                               &out_start, &out_end, &out_c2);
            if (found) {
                zm->last_center2  = (int8_t)out_c2;
                zm->track_lost_ms = 0;
            } else {
                zm->track_lost_ms = (uint16_t)(zm->track_lost_ms + (uint16_t)dt_ms);
                if (zm->track_lost_ms >= ZM_MAP1_BRANCH_HOLD_MS) {
                    DoHandover(zm); return;
                }
            }
        }

        /* Tính mask và error */
        if (found) {
            uint8_t win_mask = 0;
            uint8_t w;
            for (w = out_start; w <= out_end; ++w) win_mask |= (uint8_t)(1U << w);
            zm->last_mask = win_mask;
            computed_err = (int16_t)(((int32_t)zm->last_center2 - 7) *
                                     ZM_MAP1_ERR_PER_HALF_EYE * ZM_MAP1_ERR_SIGN);
        } else {
            /* Nếu chưa tìm thấy cụm ở ngã 3 (fork 1), mồi lái về phía target_side */
            if (zm->fork_idx >= 1U && !zm->locked) {
                computed_err = (int16_t)(zm->target_side * 1500 * ZM_MAP1_ERR_SIGN);
            } else {
                computed_err = zm->last_error;
            }
        }

        /* Giới hạn góc lái tối đa */
        if (computed_err >  (int16_t)ZM_MAP1_MAX_STEER_IN_RING)
            computed_err =  (int16_t)ZM_MAP1_MAX_STEER_IN_RING;
        if (computed_err < -(int16_t)ZM_MAP1_MAX_STEER_IN_RING)
            computed_err = -(int16_t)ZM_MAP1_MAX_STEER_IN_RING;

        /* Giới hạn tốc độ thay đổi (slew rate) */
        clamped_err = SlewLimit(zm->last_error, computed_err,
                                (int16_t)ZM_MAP1_ERR_SLEW_PER_FRAME);
        zm->last_error = clamped_err;

        if (valid) {
            output->action = ZM_ACTION_CLAMP_ERROR;
            output->error  = clamped_err;
            output->mask   = zm->last_mask ? zm->last_mask : ZM_MAP1_PASS_MASK;
        } else {
            output->action = ZM_ACTION_FORCE_STRAIGHT;
            output->error  = clamped_err;
            output->mask   = 0U;
        }

        /* Kiểm tra điều kiện thoát ngã rẽ: khi line hẹp bình thường (không còn là pattern ngã rẽ) */
        is_narrow_exit = (!is_fork) && valid && zm->locked;
        if (is_narrow_exit) {
            zm->exit_debounce_ms = (uint16_t)(zm->exit_debounce_ms + (uint16_t)dt_ms);
            if (zm->exit_debounce_ms >= ZM_MAP1_FORK_EXIT_DEBOUNCE_MS) {
                /* Đã thoát ngã rẽ hiện tại */
                ++zm->fork_idx;
                if (zm->fork_idx >= ZM_MAP1_FORK_COUNT) {
                    /* Đã xong tất cả ngã rẽ -> Handover */
                    DoHandover(zm);
                    return;
                } else {
                    /* Chờ ngã rẽ tiếp theo */
                    zm->phase            = ZM_PHASE_WAIT_FORK;
                    zm->wait_fork_ms     = 0;
                    zm->arc_candidate_ms = 0;
                    zm->side_score       = 0;
                    zm->locked           = 0;
                    zm->exit_debounce_ms = 0;
                }
            }
        } else {
            zm->exit_debounce_ms = 0;
        }

        /* Timeout ngã rẽ */
        if (zm->fork_ms >= ZM_MAP1_FORK_MAX_MS) {
            ++zm->fork_idx;
            if (zm->fork_idx >= ZM_MAP1_FORK_COUNT) {
                DoHandover(zm);
                return;
            } else {
                zm->phase            = ZM_PHASE_WAIT_FORK;
                zm->wait_fork_ms     = 0;
                zm->arc_candidate_ms = 0;
                zm->side_score       = 0;
                zm->locked           = 0;
                zm->exit_debounce_ms = 0;
            }
        }
        break;
    }

    default:
        DoHandover(zm);
        output->action = ZM_ACTION_NONE;
        break;
    }

#if (ENABLE_MAP1_OPT == 2)
    /* Shadow mode: không can thiệp thực tế */
    output->action = ZM_ACTION_NONE;
    output->error  = in_err;
    output->mask   = mask;
#endif
}
