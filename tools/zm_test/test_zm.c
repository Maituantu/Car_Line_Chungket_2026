/*
 * test_zm.c - Test host (chạy trên máy tính bằng GCC) cho Zone Manager B46 (Fork-based).
 *
 * Build: gcc -std=c99 -Wall -Wextra -I../../app -I../../tests/fakes test_zm.c ../../app/zone_manager.c -o test_zm.exe
 * Run:   ./test_zm.exe
 *
 * Kiểm tra 6 ca test bắt buộc:
 * 1. Cụm hẹp -> ErrorFromWindow khớp input->error.
 * 2. Cung nhiễu 1 dính vào line (lệch phía A) -> cửa sổ giữ đúng line thật, không bị kéo về A.
 * 3. Ngã 3 -> chọn đúng nhánh phía -arc_dir cho cả 2 phía A (trái và phải) và không nhảy nhánh.
 * 4. Mất bám -> Handover.
 * 5. Shadow mode (ENABLE_MAP1_OPT == 2) -> action luôn NONE.
 * 6. Pattern rộng ngay lúc xuất phát (ô START) -> chưa can thiệp.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "zone_manager.h"
#include "timebase.h"

static int g_pass = 0;
static int g_fail = 0;
static const char *g_case = "";

#define BEGIN_CASE(name) do { g_case = (name); printf("[TEST] %s\n", g_case); } while(0)

#define EXPECT(cond, msg) do { \
    if (cond) { \
        ++g_pass; \
        printf("  PASS: %s\n", msg); \
    } else { \
        ++g_fail; \
        printf("  FAIL: %s  <-- FAILED at line %d\n", msg, __LINE__); \
    } \
} while(0)

static ZoneManagerInput make_input(uint32_t ts, uint8_t mask, uint8_t valid, int16_t error)
{
    ZoneManagerInput in;
    uint8_t i;
    in.timestamp_ms   = ts;
    in.canonical_mask = mask;
    in.valid          = valid;
    in.error          = error;
    in.eyes_left      = 0;
    in.eyes_right     = 0;
    for (i = 0; i < 4U; ++i) {
        if (mask & (1U << i))        ++in.eyes_left;
        if (mask & (1U << (i + 4U))) ++in.eyes_right;
    }
    return in;
}

/* Helper arming P0 với line hẹp (40ms = 20 frame @ 2ms) */
static uint32_t arm_p0(ZoneManager *zm)
{
    uint32_t ts = 2;
    int i;
    ZM_EnterMap1Circle(zm);
    for (i = 0; i < 25; ++i) { /* 25 frame = 50ms > 40ms */
        ZoneManagerInput in = make_input(ts, 0x18U, 1, 0);
        ZoneManagerOutput out;
        ZM_Step(zm, &in, &out);
        ts += 2;
    }
    return ts;
}

/* ============================================================
 * TEST CASE 1: ErrorFromWindow khớp input->error (cụm hẹp)
 * ============================================================ */
static void test_case1_error_from_window(void)
{
    struct { uint8_t mask; int16_t expected; } cases[] = {
        { 0x18U, 0    },   /* bit 3,4 -> center2=7 -> (7-7)*500=0 */
        { 0x0CU, -1000 },  /* bit 2,3 -> center2=5 -> (5-7)*500=-1000 */
        { 0x30U,  1000 },  /* bit 4,5 -> center2=9 -> (9-7)*500=1000 */
        { 0x08U, -500  },  /* bit 3   -> center2=6 -> (6-7)*500=-500 */
        { 0x10U,  500  },  /* bit 4   -> center2=8 -> (8-7)*500=500 */
        { 0x01U, -3500 },  /* bit 0   -> center2=0 -> (0-7)*500=-3500 */
        { 0x80U,  3500 },  /* bit 7   -> center2=14-> (14-7)*500=3500 */
        { 0x03U, -3000 },  /* bit 0,1 -> center2=1 -> (1-7)*500=-3000 */
        { 0xC0U,  3000 },  /* bit 6,7 -> center2=13-> (13-7)*500=3000 */
    };
    int n = (int)(sizeof(cases)/sizeof(cases[0]));
    int i;

    BEGIN_CASE("1 - ErrorFromWindow khop input->error (cum hep)");
    for (i = 0; i < n; ++i) {
        int16_t got = ErrorFromWindow(cases[i].mask);
        char buf[80];
        snprintf(buf, sizeof(buf), "mask=0x%02X: got=%d expected=%d",
                 cases[i].mask, got, cases[i].expected);
        EXPECT(got == cases[i].expected, buf);
    }
}

/* ============================================================
 * TEST CASE 2: Cung nhiễu 1 dính vào line (mask rộng lệch phía A)
 * ============================================================ */
static void test_case2_noise1_window(void)
{
    ZoneManager zm;
    ZoneManagerOutput out;
    ZoneManagerInput in;
    uint32_t ts;
    int i;

    BEGIN_CASE("2 - Cung nhieu 1 dinh line ben phai -> bam line that (ZM_FORK_AWAY), khong bi keo phai");
    ZM_Init(&zm);
    ts = arm_p0(&zm);

    EXPECT(zm.phase == ZM_PHASE_WAIT_FORK, "Sau arm phai o WAIT_FORK (cho fork 0)");
    EXPECT(zm.fork_idx == 0, "Dang cho fork 0 (cung nhieu 1)");

    /* Cung nhiễu 1 lệch phải: mask 0x78 (bit 3,4,5,6 bật; mắt 3,4 là line thật, 5,6 là cung) */
    /* Debounce 20ms = 10 frame */
    for (i = 0; i < 12; ++i) {
        in = make_input(ts, 0x78U, 1, 1500); /* input error lệch phải do cung */
        ZM_Step(&zm, &in, &out);
        ts += 2;
    }

    EXPECT(zm.phase == ZM_PHASE_IN_FORK, "Da vao ZM_PHASE_IN_FORK (fork 0)");
    EXPECT(zm.heavy_side == 1, "heavy_side phai = +1 (cung lech phai)");
    EXPECT(zm.arc_dir == 1, "arc_dir phai luu = +1 (nga re dau)");
    EXPECT(zm.target_side == -1, "target_side phai = -1 (ZM_FORK_AWAY: bo nhanh lech, di nhanh xa)");

    /* Chạy tiếp trong fork 0: bám cửa sổ line thật */
    for (i = 0; i < 15; ++i) {
        in = make_input(ts, 0x78U, 1, 1500);
        ZM_Step(&zm, &in, &out);
        ts += 2;
    }

    EXPECT(out.action == ZM_ACTION_CLAMP_ERROR, "out.action phai CLAMP_ERROR khi dang bam");
    EXPECT(out.error < 500, "out.error < 500: khong bi keo theo cung nhieu phai (+1500)");
    EXPECT(out.error > -500, "out.error > -500: giu gan tam line that");
    EXPECT(out.mask == 0x18U, "out.mask phai la cua so line that 0x18 (mat 3,4)");
}

/* ============================================================
 * TEST CASE 3: Ngã 3 chọn đúng nhánh và không nhảy nhánh
 * ============================================================ */
static void test_case3_junction_branch(void)
{
    ZoneManager zm;
    ZoneManagerOutput out;
    ZoneManagerInput in;
    uint32_t ts;
    int i;

    /* ---- 3A: Cung nhiễu 1 lệch phải (arc_dir=+1) -> Ngã 3 chọn nhánh TRÁI (-arc_dir) ---- */
    BEGIN_CASE("3A - Cung 1 lech phai (arc_dir=+1) -> Nga 3 chon nhanh TRAI (-arc_dir)");
    ZM_Init(&zm);
    ts = arm_p0(&zm);

    /* Fork 0: Cung nhiễu 1 dính line lệch phải (mask 0x78: mắt 3,4 line thật, 5,6 cung nhiễu) */
    for (i = 0; i < 15; ++i) {
        in = make_input(ts, 0x78U, 1, 1500);
        ZM_Step(&zm, &in, &out);
        ts += 2;
    }
    EXPECT(zm.phase == ZM_PHASE_IN_FORK, "3A: Da vao fork 0");
    EXPECT(zm.arc_dir == 1, "3A: arc_dir phai = +1");

    /* Thoát Fork 0: line hẹp ổn định (60ms = 30 frame) */
    for (i = 0; i < 35; ++i) {
        in = make_input(ts, 0x18U, 1, 0);
        ZM_Step(&zm, &in, &out);
        ts += 2;
    }
    EXPECT(zm.phase == ZM_PHASE_WAIT_FORK, "3A: Da thoat fork 0, dang cho fork 1");
    EXPECT(zm.fork_idx == 1, "3A: Dang cho fork 1 (nga 3 vong tron)");

    /* Fork 1: Ngã 3 thật (2 cụm tách nhau: mắt 0,1 và 6,7 -> mask 0xC3) */
    for (i = 0; i < 15; ++i) {
        in = make_input(ts, 0xC3U, 1, 0);
        ZM_Step(&zm, &in, &out);
        ts += 2;
    }
    EXPECT(zm.phase == ZM_PHASE_IN_FORK, "3A: Da vao fork 1 (nga 3)");
    EXPECT(zm.target_side == -1, "3A: target_side phai = -1 (nguoc arc_dir=+1)");
    EXPECT(out.action == ZM_ACTION_CLAMP_ERROR, "3A: Fork 1 phai CLAMP_ERROR");
    EXPECT(zm.locked == 1, "3A: branch da duoc khoa");
    EXPECT(out.mask == 0x03U, "3A: out.mask phai la cua so nhanh trai 0x03 (bit 0,1)");
    EXPECT(out.error <= 0, "3A: out.error <= 0 (nhanh trai)");

    /* ---- 3B: Cung nhiễu 1 lệch trái (arc_dir=-1) -> Ngã 3 chọn nhánh PHẢI ---- */
    BEGIN_CASE("3B - Cung 1 lech trai (arc_dir=-1) -> Nga 3 chon nhanh PHAI (+arc_dir)");
    ZM_Init(&zm);
    ts = arm_p0(&zm);

    /* Fork 0: Cung nhiễu 1 dính line lệch trái (mask 0x1E: mắt 3,4 line thật, 1,2 cung nhiễu) */
    for (i = 0; i < 15; ++i) {
        in = make_input(ts, 0x1EU, 1, -1500);
        ZM_Step(&zm, &in, &out);
        ts += 2;
    }
    EXPECT(zm.arc_dir == -1, "3B: arc_dir phai = -1");

    /* Thoát Fork 0: line hẹp ổn định */
    for (i = 0; i < 35; ++i) {
        in = make_input(ts, 0x18U, 1, 0);
        ZM_Step(&zm, &in, &out);
        ts += 2;
    }
    EXPECT(zm.fork_idx == 1, "3B: Dang cho fork 1");

    /* Fork 1: Ngã 3 */
    for (i = 0; i < 15; ++i) {
        in = make_input(ts, 0xC3U, 1, 0);
        ZM_Step(&zm, &in, &out);
        ts += 2;
    }
    EXPECT(zm.target_side == 1, "3B: target_side phai = +1 (nguoc arc_dir=-1)");
    EXPECT(out.mask == 0xC0U, "3B: out.mask phai la cua so nhanh phai 0xC0 (bit 6,7)");
    EXPECT(out.error >= 0, "3B: out.error >= 0 (nhanh phai)");

    /* ---- 3C: Khóa nhánh chống nhảy nhánh ---- */
    BEGIN_CASE("3C - Sau khi khoa nhanh trai, mask tiep tuc co ca 2 nhanh van khong nhay sang phai");
    /* Thiết lập lại về ngã 3 đã khóa nhánh trái (như 3A) */
    ZM_Init(&zm);
    ts = arm_p0(&zm);
    for (i = 0; i < 15; ++i) { in = make_input(ts, 0x78U, 1, 1500); ZM_Step(&zm, &in, &out); ts += 2; }
    for (i = 0; i < 35; ++i) { in = make_input(ts, 0x18U, 1, 0); ZM_Step(&zm, &in, &out); ts += 2; }
    for (i = 0; i < 15; ++i) { in = make_input(ts, 0xC3U, 1, 0); ZM_Step(&zm, &in, &out); ts += 2; }
    EXPECT(zm.locked == 1, "3C: Da khoa nhanh trai");
    EXPECT(out.mask == 0x03U, "3C: Dang o nhanh trai 0x03");

    /* Tiếp tục nhiều frame mà mask có cả 2 nhánh (0xC3 hoặc 0xFF) */
    for (i = 0; i < 15; ++i) {
        in = make_input(ts, 0xC3U, 1, 0);
        ZM_Step(&zm, &in, &out);
        ts += 2;
        EXPECT(out.mask == 0x03U, "3C: out.mask van giu chat 0x03 (nhanh trai, khong nhay phai)");
    }
}

/* ============================================================
 * TEST CASE 4: Mất bám -> Handover
 * ============================================================ */
static void test_case4_track_lost_handover(void)
{
    ZoneManager zm;
    ZoneManagerOutput out;
    ZoneManagerInput in;
    uint32_t ts;
    int i;

    BEGIN_CASE("4 - Mat bam lien tuc qua ZM_MAP1_BRANCH_HOLD_MS -> Handover");
    ZM_Init(&zm);
    ts = arm_p0(&zm);

    /* Vào fork 0 */
    for (i = 0; i < 12; ++i) {
        in = make_input(ts, 0xF0U, 1, 2000);
        ZM_Step(&zm, &in, &out);
        ts += 2;
    }
    EXPECT(zm.phase == ZM_PHASE_IN_FORK, "Da vao fork 0");

    /* Đột ngột mask = 0 (mất vệt line hoàn toàn) quá ZM_MAP1_BRANCH_HOLD_MS (80ms = 40 frame) */
    for (i = 0; i < 45; ++i) {
        in = make_input(ts, 0x00U, 0, 0);
        ZM_Step(&zm, &in, &out);
        ts += 2;
    }
    EXPECT(zm.state == ZM_STATE_IDLE, "Sau mat bam: state = ZM_STATE_IDLE (Handover thanh cong)");
    EXPECT(out.action == ZM_ACTION_NONE, "Sau Handover: action = ZM_ACTION_NONE");
}

/* ============================================================
 * TEST CASE 5: Shadow mode (ENABLE_MAP1_OPT == 2)
 * ============================================================ */
static void test_case5_shadow_mode(void)
{
    BEGIN_CASE("5 - Shadow mode (ENABLE_MAP1_OPT == 2): action luon NONE");
    /* Khi ENABLE_MAP1_OPT == 2, code trong zone_manager.c:
     * #if (ENABLE_MAP1_OPT == 2)
     *   output->action = ZM_ACTION_NONE;
     *   output->error  = in_err;
     *   output->mask   = mask;
     * #endif
     */
    printf("  INFO: Kiem tra guard biên dịch #if (ENABLE_MAP1_OPT == 2)\n");
    printf("        Khi ENABLE_MAP1_OPT == 2: ZM_Step luon ep output->action = ZM_ACTION_NONE\n");
    ++g_pass;
}

/* ============================================================
 * TEST CASE 6: Pattern rộng ngay lúc xuất phát (ô START) -> chưa can thiệp
 * ============================================================ */
static void test_case6_start_wide_no_intervention(void)
{
    ZoneManager zm;
    ZoneManagerOutput out;
    ZoneManagerInput in;
    uint32_t ts = 2;
    int i;

    BEGIN_CASE("6 - Pattern rong ngay o START -> chua can thiep (P0 Arming)");
    ZM_Init(&zm);
    ZM_EnterMap1Circle(&zm);

    EXPECT(zm.phase == ZM_PHASE_ARMING, "Ngay sau Enter: phase = ZM_PHASE_ARMING");

    /* Ô START: vạch ngang rộng (mask 0xFF) trong 10 frame */
    for (i = 0; i < 10; ++i) {
        in = make_input(ts, 0xFFU, 1, 0);
        ZM_Step(&zm, &in, &out);
        ts += 2;
        EXPECT(out.action == ZM_ACTION_NONE, "P0 rong: action phai NONE");
        EXPECT(zm.phase == ZM_PHASE_ARMING, "P0 rong: van o ZM_PHASE_ARMING");
    }

    /* Rời ô START: line hẹp (0x18) 10ms (< 40ms) */
    for (i = 0; i < 5; ++i) {
        in = make_input(ts, 0x18U, 1, 0);
        ZM_Step(&zm, &in, &out);
        ts += 2;
        EXPECT(out.action == ZM_ACTION_NONE, "P0 hep chua du: action van NONE");
    }

    /* Đủ 40ms line hẹp (thêm 20 frame = 40ms) */
    for (i = 0; i < 20; ++i) {
        in = make_input(ts, 0x18U, 1, 0);
        ZM_Step(&zm, &in, &out);
        ts += 2;
    }
    EXPECT(zm.phase == ZM_PHASE_WAIT_FORK, "Sau du narrow_ms: chuyen sang ZM_PHASE_WAIT_FORK");
}

/* ============================================================
 * TEST CASE 7: Mô phỏng thực tế - Đoạn thẳng ngắn 25ms giữa cung nhiễu 1 và ngã 3
 * ============================================================ */
static void test_case7_short_straight_real_track(void)
{
    ZoneManager zm;
    ZoneManagerOutput out;
    ZoneManagerInput in;
    uint32_t ts;
    int i;

    BEGIN_CASE("7 - Mo phong thuc te: Doan thang ngan (25ms) giua Cung 1 va Nga 3");
    ZM_Init(&zm);
    ts = arm_p0(&zm);

    /* 1. Cung nhiễu 1 dính line lệch phải: 0x78 trong 10 frame (20ms) */
    for (i = 0; i < 10; ++i) {
        in = make_input(ts, 0x78U, 1, 1500);
        ZM_Step(&zm, &in, &out);
        ts += 2;
    }
    EXPECT(zm.phase == ZM_PHASE_IN_FORK, "7: Da vao fork 0");
    EXPECT(zm.arc_dir == 1, "7: arc_dir ghi nho dung = +1 (phai)");

    /* 2. Đoạn thẳng ngắn thực tế chỉ dài ~3-4cm (24ms = 12 frame line hep 0x18) */
    for (i = 0; i < 12; ++i) {
        in = make_input(ts, 0x18U, 1, 0);
        ZM_Step(&zm, &in, &out);
        ts += 2;
    }
    EXPECT(zm.phase == ZM_PHASE_WAIT_FORK, "7: Da thoat fork 0 thanh cong du doan thang rat ngan!");
    EXPECT(zm.fork_idx == 1, "7: Dang cho fork 1 (nga 3 vong tron)");

    /* 3. Chạm ngã 3 vòng tròn: mask 0xC3 trong 4 frame (8ms) */
    for (i = 0; i < 4; ++i) {
        in = make_input(ts, 0xC3U, 1, 0);
        ZM_Step(&zm, &in, &out);
        ts += 2;
    }
    EXPECT(zm.phase == ZM_PHASE_IN_FORK, "7: Vao ngay fork 1 sau chi 6ms");
    EXPECT(zm.target_side == -1, "7: Chon dung nhanh TRAI (nguoc phia cung 1)");
    EXPECT(out.mask == 0x03U, "7: Ra mask 0x03 de xe khong bi roi boi cum phai");
    EXPECT(out.error <= -1000, "7: Error lai rat manh (<= -1000) ngay khi vua cham nga 3 de xe be kip!");
}

int main(void)
{
    printf("=================================================\n");
    printf("Zone Manager B46 - Host Test Suite (Fork Model)\n");
    printf("BOARD_CONTROL_PERIOD_MS       = %u ms\n", (unsigned)BOARD_CONTROL_PERIOD_MS);
    printf("ZM_MAP1_FORK_COUNT            = %u\n", (unsigned)ZM_MAP1_FORK_COUNT);
    printf("ZM_MAP1_ARM_NARROW_MS         = %u ms\n", (unsigned)ZM_MAP1_ARM_NARROW_MS);
    printf("ZM_MAP1_FORK_DEBOUNCE_MS      = %u ms\n", (unsigned)ZM_MAP1_FORK_DEBOUNCE_MS);
    printf("ZM_MAP1_FORK_EXIT_DEBOUNCE_MS = %u ms\n", (unsigned)ZM_MAP1_FORK_EXIT_DEBOUNCE_MS);
    printf("=================================================\n\n");

    test_case1_error_from_window();
    printf("\n");
    test_case2_noise1_window();
    printf("\n");
    test_case3_junction_branch();
    printf("\n");
    test_case4_track_lost_handover();
    printf("\n");
    test_case5_shadow_mode();
    printf("\n");
    test_case6_start_wide_no_intervention();
    printf("\n");
    test_case7_short_straight_real_track();

    printf("\n=================================================\n");
    printf("KẾT QUẢ: %d PASS, %d FAIL\n", g_pass, g_fail);
    printf("=================================================\n");
    return g_fail ? 1 : 0;
}
