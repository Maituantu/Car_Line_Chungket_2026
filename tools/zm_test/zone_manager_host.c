/*
 * zone_manager_host.c - Bản sao zone_manager.c cho build host (máy tính).
 *
 * Chỉ khác zone_manager.c ở chỗ:
 * 1. Không #include "zone_manager.h" trực tiếp từ app/ mà dùng wrapper.
 * 2. #include "zone_manager_host.h" thay thế (cung cấp các fake define).
 * 3. ErrorFromWindow được khai báo non-static để test_zm.c có thể gọi.
 *
 * Cách build:
 *   gcc -std=c99 -Wall -Wextra -DENABLE_MAP1_OPT=1 -DBOARD_CONTROL_PERIOD_MS=2
 *       zone_manager_host.c test_zm.c -o test_zm
 *
 * KHÔNG đưa file này vào build firmware Keil.
 */

#include "zone_manager_host.h"

/* ============================================================
 * MACRO NỘI BỘ (sao chép từ zone_manager.c)
 * ============================================================ */

static uint8_t CountBits(uint8_t mask)
{
    uint8_t count = 0;
    while (mask) { count += mask & 1U; mask >>= 1; }
    return count;
}

static uint8_t IsWidePattern(uint8_t mask)
{
    return CountBits(mask) > ZM_MAP1_NARROW_MAX_EYES;
}

static uint8_t IsJunction(uint8_t mask)
{
    return ((mask & 0x0FU) != 0U) && ((mask & 0xF0U) != 0U);
}

/* ============================================================
 * HELPER 1: FindClusters
 * ============================================================ */
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

/* ============================================================
 * HELPER 2: PickWindow
 * ============================================================ */
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

    if (width_eyes == 0U || width_eyes > 8U) return 0;

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

    if (!found) return 0;

    *out_start   = best_start;
    *out_end     = best_end;
    *out_center2 = best_c2;

    return (best_dist <= (int16_t)ZM_MAP1_TRACK_MAX_JUMP) ? 1U : 0U;
}

/* ============================================================
 * HELPER 3: PickWindowAtSide
 * ============================================================ */
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
    } else {
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
    }
    return 0U;
}

/* ============================================================
 * HELPER 4: ErrorFromWindow (non-static để test_zm.c gọi được)
 * ============================================================ */
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
    return (int16_t)((c2 - 7) * 500);
}

/* ============================================================
 * HELPER 5: SlewLimit
 * ============================================================ */
static int16_t SlewLimit(int16_t current, int16_t target, int16_t max_step)
{
    int16_t diff = (int16_t)(target - current);
    if (diff >  max_step) return (int16_t)(current + max_step);
    if (diff < -max_step) return (int16_t)(current - max_step);
    return target;
}

/* ============================================================
 * NỘI BỘ: Handover
 * ============================================================ */
static void DoHandover(ZoneManager *zm)
{
    zm->state              = ZM_STATE_IDLE;
    zm->phase              = ZM_PHASE_HANDOVER;
    zm->window_valid       = 0;
    zm->track_lost_ms      = 0;
}

/* ============================================================
 * NỘI BỘ: EnterPhase
 * ============================================================ */
static void EnterPhase(ZoneManager *zm, ZonePhase new_phase)
{
    zm->phase          = new_phase;
    zm->phase_time_ms  = 0;
    zm->narrow_ms      = 0;
    zm->wide_ms        = 0;
    zm->narrow_exit_ms = 0;
    zm->junction_ms    = 0;
    zm->branch_exit_ms = 0;
    zm->blind_ms       = 0;
}

/* ============================================================
 * NỘI BỘ: PushLog (stub trên host - không làm gì)
 * ============================================================ */
static void PushLog(ZoneManager *zm,
                    uint32_t timestamp_ms,
                    uint8_t  raw_mask,
                    uint8_t  sanitized_mask,
                    int16_t  in_error,
                    int16_t  out_error)
{
    ZoneManagerLogEntry *e = &zm->log_history[zm->log_head];
    e->timestamp_ms  = timestamp_ms;
    e->phase         = (uint8_t)zm->phase;
    e->arc_dir       = zm->arc_dir;
    e->raw_mask      = raw_mask;
    e->sanitized_mask = sanitized_mask;
    e->window_start  = zm->window_start;
    e->window_end    = zm->window_end;
    e->last_center2  = zm->last_center2;
    e->track_lost_ms = zm->track_lost_ms;
    e->in_error      = in_error;
    e->out_error     = out_error;
    e->phase_time_ms = zm->phase_time_ms;
    zm->log_head = (uint8_t)((zm->log_head + 1U) % ZM_LOG_CAPACITY);
}

#define ZM_LOG_PERIOD_MS 20U

/* ============================================================
 * API: ZM_Init
 * ============================================================ */
void ZM_Init(ZoneManager *zm)
{
    static const ZoneManager zero = {0};
    *zm            = zero;
    zm->state      = ZM_STATE_IDLE;
    zm->phase      = ZM_PHASE_HANDOVER;
    zm->last_center2 = 7;
    zm->log_head   = 0;
    zm->log_last_ms = 0;
}

/* ============================================================
 * API: ZM_EnterMap1Circle
 * ============================================================ */
void ZM_EnterMap1Circle(ZoneManager *zm)
{
    static const ZoneManager zero = {0};
    *zm = zero;
    zm->state         = ZM_STATE_M1_CIRCLE;
    zm->last_center2  = 7;
    zm->last_valid_error = 0;
    zm->window_valid  = 0;
    zm->arc_dir       = 0;
    zm->arc_dir_locked = 0;
    zm->branch_locked = 0;
    zm->log_head      = 0;
    zm->log_last_ms   = 0;
    EnterPhase(zm, ZM_PHASE_P0_ARMING);
}

/* ============================================================
 * API: ZM_Step
 * ============================================================ */
void ZM_Step(ZoneManager *zm,
             const ZoneManagerInput *input,
             ZoneManagerOutput *output)
{
    uint8_t  mask    = input->canonical_mask;
    uint8_t  valid   = input->valid;
    int16_t  in_err  = input->error;
    uint32_t dt_ms   = BOARD_CONTROL_PERIOD_MS;
    uint8_t  is_wide = IsWidePattern(mask);
    uint8_t  is_jct  = IsJunction(mask);
    uint8_t  out_start = 0, out_end = 0;
    int16_t  out_c2  = 0;
    int16_t  computed_err;
    int16_t  clamped_err;
    uint8_t  out_mask;
    uint8_t  should_log;
    uint8_t  window_found;
    ZonePhase prev_phase = zm->phase;

    output->action = ZM_ACTION_NONE;
    output->error  = in_err;
    output->mask   = mask;

    if (zm->state == ZM_STATE_IDLE) return;

    zm->phase_time_ms += dt_ms;

    switch (zm->phase) {

    case ZM_PHASE_P0_ARMING:
    {
        if (!is_wide && valid) {
            uint8_t f[8], l[8];
            uint8_t nc = FindClusters(mask, f, l, 8U);
            if (nc == 1U) {
                zm->last_center2    = (int16_t)(f[0] + l[0]);
                zm->last_valid_error = in_err;
                zm->window_start    = f[0];
                zm->window_end      = l[0];
                zm->window_valid    = 1;
                zm->track_lost_ms   = 0;
            }
            zm->narrow_ms += dt_ms;
        } else {
            zm->narrow_ms = 0;
        }

        if (zm->narrow_ms >= ZM_MAP1_ARM_NARROW_MS) {
            EnterPhase(zm, ZM_PHASE_P1_NOISE1);
        } else if (zm->phase_time_ms >= ZM_MAP1_ARM_MAX_MS) {
            DoHandover(zm);
        }
        output->action = ZM_ACTION_NONE;
        break;
    }

    case ZM_PHASE_P1_NOISE1:
    {
        uint32_t timeout_check;

        if (is_wide) {
            zm->narrow_exit_ms = 0;

            window_found = PickWindow(mask, zm->last_center2,
                                      ZM_MAP1_LINE_WIDTH_EYES,
                                      &out_start, &out_end, &out_c2);

            if (window_found) {
                zm->last_center2    = out_c2;
                zm->window_start    = out_start;
                zm->window_end      = out_end;
                zm->window_valid    = 1;
                zm->track_lost_ms   = 0;
            } else {
                zm->track_lost_ms = (uint16_t)(zm->track_lost_ms + (uint16_t)dt_ms);
                if (zm->track_lost_ms > (uint16_t)65000U) zm->track_lost_ms = 65000U;
            }

            zm->wide_ms += dt_ms;
            if (!zm->arc_dir_locked && zm->wide_ms >= ZM_MAP1_ARC_DEBOUNCE_MS) {
                int8_t side;
                if (input->eyes_right > input->eyes_left) side = 1;
                else if (input->eyes_left > input->eyes_right) side = -1;
                else side = (zm->last_center2 > 7) ? 1 : -1;
                zm->arc_dir        = side;
                zm->arc_dir_locked = 1;
            }

            if (zm->window_valid) {
                uint8_t win_mask = 0;
                uint8_t w;
                for (w = zm->window_start; w <= zm->window_end; ++w)
                    win_mask |= (uint8_t)(1U << w);
                computed_err = ErrorFromWindow(win_mask);
            } else {
                computed_err = zm->last_valid_error;
            }

            if (computed_err >  (int16_t)ZM_MAP1_MAX_STEER_IN_RING)
                computed_err =  (int16_t)ZM_MAP1_MAX_STEER_IN_RING;
            if (computed_err < -(int16_t)ZM_MAP1_MAX_STEER_IN_RING)
                computed_err = -(int16_t)ZM_MAP1_MAX_STEER_IN_RING;

            clamped_err = SlewLimit(zm->last_valid_error, computed_err,
                                    (int16_t)ZM_MAP1_ERR_SLEW_PER_FRAME);
            zm->last_valid_error = clamped_err;

            if (valid) {
                output->action = ZM_ACTION_CLAMP_ERROR;
                output->error  = clamped_err;
            } else {
                zm->blind_ms += dt_ms;
                if (zm->blind_ms >= ZM_MAP1_BLIND_MAX_MS) {
                    DoHandover(zm); goto log_and_return;
                }
                output->action = ZM_ACTION_FORCE_STRAIGHT;
                output->error  = clamped_err;
            }

            out_mask = 0;
            if (zm->window_valid) {
                uint8_t w;
                for (w = zm->window_start; w <= zm->window_end; ++w)
                    out_mask |= (uint8_t)(1U << w);
            } else {
                out_mask = mask;
            }
            output->mask = out_mask;

        } else {
            zm->wide_ms = 0;
            zm->blind_ms = 0;

            if (valid) {
                uint8_t f[8], l[8];
                uint8_t nc = FindClusters(mask, f, l, 8U);
                if (nc == 1U) {
                    zm->last_center2    = (int16_t)(f[0] + l[0]);
                    zm->last_valid_error = in_err;
                    zm->window_start    = f[0];
                    zm->window_end      = l[0];
                    zm->window_valid    = 1;
                    zm->track_lost_ms   = 0;
                }
                zm->narrow_exit_ms += dt_ms;
            } else {
                zm->narrow_exit_ms = 0;
            }

            if (zm->arc_dir_locked &&
                zm->narrow_exit_ms >= ZM_MAP1_CIRCLE_EXIT_DEBOUNCE_MS) {
                EnterPhase(zm, ZM_PHASE_P2_WAIT_JCT);
            }

            output->action = ZM_ACTION_NONE;
        }

        timeout_check = zm->arc_dir_locked ? 0U : ZM_MAP1_ARC_WAIT_MAX_MS;
        if (!zm->arc_dir_locked && zm->phase_time_ms >= timeout_check && timeout_check > 0U) {
            DoHandover(zm); goto log_and_return;
        }
        if (zm->phase_time_ms >= ZM_MAP1_NOISE1_MAX_MS) {
            DoHandover(zm); goto log_and_return;
        }
        if (zm->track_lost_ms >= ZM_MAP1_TRACK_LOST_MS) {
            DoHandover(zm); goto log_and_return;
        }
        break;
    }

    case ZM_PHASE_P2_WAIT_JCT:
    {
        if (!is_wide && valid) {
            uint8_t f[8], l[8];
            uint8_t nc = FindClusters(mask, f, l, 8U);
            if (nc == 1U) {
                zm->last_center2    = (int16_t)(f[0] + l[0]);
                zm->last_valid_error = in_err;
                zm->window_start    = f[0];
                zm->window_end      = l[0];
                zm->window_valid    = 1;
                zm->track_lost_ms   = 0;
            }
        }

        if (is_jct || is_wide) {
            zm->junction_ms += dt_ms;
        } else {
            zm->junction_ms = 0;
        }

        if (zm->junction_ms >= ZM_MAP1_JUNCTION_DEBOUNCE_MS) {
            EnterPhase(zm, ZM_PHASE_P3_TAKE_BRCH);
            zm->branch_locked = 0;
        }

        if (zm->phase_time_ms >= ZM_MAP1_JUNCTION_WAIT_MAX_MS) {
            DoHandover(zm); goto log_and_return;
        }

        output->action = ZM_ACTION_NONE;
        break;
    }

    case ZM_PHASE_P3_TAKE_BRCH:
    {
        int8_t side_b = (int8_t)(-zm->arc_dir);
        uint8_t win_mask = 0;
        uint8_t w;

        if (valid || is_wide) {
            if (!zm->branch_locked) {
                window_found = PickWindowAtSide(mask, side_b,
                                                ZM_MAP1_LINE_WIDTH_EYES,
                                                &out_start, &out_end, &out_c2);
                if (window_found) {
                    zm->last_center2    = out_c2;
                    zm->window_start    = out_start;
                    zm->window_end      = out_end;
                    zm->window_valid    = 1;
                    zm->track_lost_ms   = 0;
                    zm->branch_locked   = 1;
                } else {
                    zm->track_lost_ms = (uint16_t)(zm->track_lost_ms + (uint16_t)dt_ms);
                    if (zm->track_lost_ms > (uint16_t)65000U) zm->track_lost_ms = 65000U;
                }
            } else {
                window_found = PickWindow(mask, zm->last_center2,
                                          ZM_MAP1_LINE_WIDTH_EYES,
                                          &out_start, &out_end, &out_c2);
                if (window_found) {
                    zm->last_center2    = out_c2;
                    zm->window_start    = out_start;
                    zm->window_end      = out_end;
                    zm->window_valid    = 1;
                    zm->track_lost_ms   = 0;
                } else {
                    zm->track_lost_ms = (uint16_t)(zm->track_lost_ms + (uint16_t)dt_ms);
                    if (zm->track_lost_ms > (uint16_t)65000U) zm->track_lost_ms = 65000U;
                }
            }

            if (zm->window_valid) {
                for (w = zm->window_start; w <= zm->window_end; ++w)
                    win_mask |= (uint8_t)(1U << w);
                computed_err = ErrorFromWindow(win_mask);
            } else {
                computed_err = zm->last_valid_error;
            }

            if (computed_err >  (int16_t)ZM_MAP1_MAX_STEER_IN_RING)
                computed_err =  (int16_t)ZM_MAP1_MAX_STEER_IN_RING;
            if (computed_err < -(int16_t)ZM_MAP1_MAX_STEER_IN_RING)
                computed_err = -(int16_t)ZM_MAP1_MAX_STEER_IN_RING;

            clamped_err = SlewLimit(zm->last_valid_error, computed_err,
                                    (int16_t)ZM_MAP1_ERR_SLEW_PER_FRAME);
            zm->last_valid_error = clamped_err;

            output->action = ZM_ACTION_CLAMP_ERROR;
            output->error  = clamped_err;
            output->mask   = win_mask ? win_mask : mask;

            if (!zm->branch_locked) {
                if (zm->track_lost_ms >= ZM_MAP1_BRANCH_HOLD_MS) {
                    DoHandover(zm); goto log_and_return;
                }
            }

            zm->blind_ms = 0;

            if (!is_wide && !is_jct && zm->branch_locked && valid) {
                zm->branch_exit_ms += dt_ms;
            } else {
                zm->branch_exit_ms = 0;
            }

            if (zm->branch_exit_ms >= ZM_MAP1_BRANCH_EXIT_DEBOUNCE_MS) {
                EnterPhase(zm, ZM_PHASE_P4_NOISE2);
            }

        } else {
            zm->blind_ms += dt_ms;
            if (zm->blind_ms >= ZM_MAP1_BLIND_MAX_MS) {
                DoHandover(zm); goto log_and_return;
            }
            clamped_err = zm->last_valid_error;
            output->action = ZM_ACTION_FORCE_STRAIGHT;
            output->error  = clamped_err;
            output->mask   = 0U;
        }

        if (zm->phase_time_ms >= ZM_MAP1_BRANCH_MAX_MS) {
            DoHandover(zm); goto log_and_return;
        }
        if (zm->track_lost_ms >= ZM_MAP1_TRACK_LOST_MS) {
            DoHandover(zm); goto log_and_return;
        }
        break;
    }

    case ZM_PHASE_P4_NOISE2:
    {
        if (zm->phase_time_ms >= ZM_MAP1_NOISE2_IGNORE_MS) {
            DoHandover(zm); goto log_and_return;
        }

        if (is_wide) {
            uint8_t win_mask = 0;
            uint8_t w;

            window_found = PickWindow(mask, zm->last_center2,
                                      ZM_MAP1_LINE_WIDTH_EYES,
                                      &out_start, &out_end, &out_c2);
            if (window_found) {
                zm->last_center2    = out_c2;
                zm->window_start    = out_start;
                zm->window_end      = out_end;
                zm->window_valid    = 1;
                zm->track_lost_ms   = 0;
            } else {
                zm->track_lost_ms = (uint16_t)(zm->track_lost_ms + (uint16_t)dt_ms);
                if (zm->track_lost_ms > (uint16_t)65000U) zm->track_lost_ms = 65000U;
            }

            if (zm->window_valid) {
                for (w = zm->window_start; w <= zm->window_end; ++w)
                    win_mask |= (uint8_t)(1U << w);
                computed_err = ErrorFromWindow(win_mask);
            } else {
                computed_err = zm->last_valid_error;
            }

            if (computed_err >  (int16_t)ZM_MAP1_MAX_STEER_IN_RING)
                computed_err =  (int16_t)ZM_MAP1_MAX_STEER_IN_RING;
            if (computed_err < -(int16_t)ZM_MAP1_MAX_STEER_IN_RING)
                computed_err = -(int16_t)ZM_MAP1_MAX_STEER_IN_RING;

            clamped_err = SlewLimit(zm->last_valid_error, computed_err,
                                    (int16_t)ZM_MAP1_ERR_SLEW_PER_FRAME);
            zm->last_valid_error = clamped_err;

            if (valid) {
                output->action = ZM_ACTION_CLAMP_ERROR;
                output->error  = clamped_err;
            } else {
                zm->blind_ms += dt_ms;
                if (zm->blind_ms >= ZM_MAP1_BLIND_MAX_MS) {
                    DoHandover(zm); goto log_and_return;
                }
                output->action = ZM_ACTION_FORCE_STRAIGHT;
                output->error  = clamped_err;
            }
            output->mask = win_mask ? win_mask : mask;

        } else {
            zm->blind_ms = 0;
            if (valid) {
                uint8_t f[8], l[8];
                uint8_t nc = FindClusters(mask, f, l, 8U);
                if (nc == 1U) {
                    zm->last_center2    = (int16_t)(f[0] + l[0]);
                    zm->last_valid_error = in_err;
                    zm->window_start    = f[0];
                    zm->window_end      = l[0];
                    zm->window_valid    = 1;
                    zm->track_lost_ms   = 0;
                }
            }
            output->action = ZM_ACTION_NONE;
        }

        if (zm->track_lost_ms >= ZM_MAP1_TRACK_LOST_MS) {
            DoHandover(zm); goto log_and_return;
        }
        break;
    }

    case ZM_PHASE_HANDOVER:
    default:
        output->action = ZM_ACTION_NONE;
        break;
    }

log_and_return:
    should_log = (zm->phase != prev_phase);
    if (!should_log) {
        uint32_t elapsed = input->timestamp_ms - zm->log_last_ms;
        if (elapsed >= ZM_LOG_PERIOD_MS) should_log = 1U;
    }
    if (should_log) {
        uint8_t san_mask = output->mask;
        PushLog(zm, input->timestamp_ms, mask, san_mask,
                in_err, output->error);
        zm->log_last_ms = input->timestamp_ms;
    }

#if (ENABLE_MAP1_OPT == 2)
    output->action = ZM_ACTION_NONE;
    output->error  = in_err;
    output->mask   = mask;
#endif
}

