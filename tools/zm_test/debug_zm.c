/* Debug test để hiểu state transitions */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#ifndef BOARD_CONTROL_PERIOD_MS
#define BOARD_CONTROL_PERIOD_MS 2U
#endif
#ifndef ENABLE_MAP1_OPT
#define ENABLE_MAP1_OPT 1
#endif
#ifndef ZM_MAP1_SANITIZE_MASK
#define ZM_MAP1_SANITIZE_MASK 1
#endif
#include "zone_manager_host.h"

static ZoneManagerInput make_input(uint32_t ts, uint8_t mask, uint8_t valid, int16_t error)
{
    ZoneManagerInput in; uint8_t i;
    in.timestamp_ms=ts; in.canonical_mask=mask; in.valid=valid; in.error=error;
    in.eyes_left=0; in.eyes_right=0;
    for(i=0;i<4;++i){ if(mask&(1U<<i)) ++in.eyes_left; if(mask&(1U<<(i+4))) ++in.eyes_right; }
    return in;
}

int main(void){
    ZoneManager zm;
    ZoneManagerOutput out;
    ZoneManagerInput in;
    uint32_t ts=2;
    int i;

    printf("=== Debug Test 3A ===\n");
    ZM_Init(&zm);
    ZM_EnterMap1Circle(&zm);

    /* P0: 0x30 (bit2,3 -> wrong, should be narrow) */
    for(i=0;i<20;++i){
        in=make_input(ts,0x30U,1,1000);
        ZM_Step(&zm,&in,&out);
        ts+=2;
    }
    printf("After P0: phase=%d, last_center2=%d, narrow_ms=%u\n",
           zm.phase, zm.last_center2, (unsigned)zm.narrow_ms);

    /* P1: 0xF0 wide */
    for(i=0;i<15;++i){
        in=make_input(ts,0xF0U,1,2000);
        ZM_Step(&zm,&in,&out);
        ts+=2;
    }
    printf("After P1 wide: phase=%d, arc_dir=%d, locked=%d, wide_ms=%u, last_c2=%d\n",
           zm.phase,zm.arc_dir,zm.arc_dir_locked,(unsigned)zm.wide_ms,zm.last_center2);

    /* Thoát P1: 0x18 narrow */
    printf("Trying to exit P1 with narrow 0x18 (need arc_locked + narrow_exit_ms >= %u):\n",
           (unsigned)ZM_MAP1_CIRCLE_EXIT_DEBOUNCE_MS);
    for(i=0;i<15;++i){
        in=make_input(ts,0x18U,1,0);
        ZM_Step(&zm,&in,&out);
        ts+=2;
        printf("  frame %d: phase=%d, narrow_exit_ms=%u, arc_locked=%d\n",
               i, zm.phase, (unsigned)zm.narrow_exit_ms, zm.arc_dir_locked);
        if(zm.phase==ZM_PHASE_P2_WAIT_JCT) break;
    }
    printf("After narrow: phase=%d (expected %d=P2)\n", zm.phase, ZM_PHASE_P2_WAIT_JCT);

    /* P2 junction */
    for(i=0;i<6;++i){
        in=make_input(ts,0xFFU,1,0);
        ZM_Step(&zm,&in,&out);
        ts+=2;
    }
    printf("After P2 junction: phase=%d (expected %d=P3)\n", zm.phase, ZM_PHASE_P3_TAKE_BRCH);

    /* P3: pick branch */
    for(i=0;i<5;++i){
        in=make_input(ts,0xFFU,1,0);
        ZM_Step(&zm,&in,&out);
        ts+=2;
    }
    printf("P3 result: action=%d, error=%d, mask=0x%02X, branch_locked=%d\n",
           out.action, out.error, out.mask, zm.branch_locked);
    printf("P3 window: start=%d end=%d center2=%d\n",
           zm.window_start, zm.window_end, zm.last_center2);

    printf("\n=== Debug Test 4 (Track Lost) ===\n");
    ZM_Init(&zm); ts=2;
    ZM_EnterMap1Circle(&zm);
    /* P0 arm */
    for(i=0;i<20;++i){ in=make_input(ts,0x18U,1,0); ZM_Step(&zm,&in,&out); ts+=2; }
    printf("After P0 arm: phase=%d, last_c2=%d\n", zm.phase, zm.last_center2);

    /* 0x81 = bit0 + bit7: wide(2 bits > NARROW=3? No: 2 <= 3 -> NOT wide!) */
    {
        uint8_t cnt=0; uint8_t b; uint8_t m=0x81U;
        for(b=0;b<8;++b) if(m&(1U<<b)) ++cnt;
        printf("0x81 bit count=%d (NARROW_MAX=%d, IsWide=%d)\n",
               cnt, (int)ZM_MAP1_NARROW_MAX_EYES, cnt>(int)ZM_MAP1_NARROW_MAX_EYES);
    }
    return 0;
}

