// generative - where main firmware 2.5.20 keeps what generative.c and hooks.S use, per model. The Chroma has its own build: its
// code sits elsewhere and its RAM below 0x20006320 moved up by 4 or 0x20 bytes. The Chroma column was mapped from the
// standard one by matching functions across the two builds (bodies hashed with addresses masked, then the call
// graph) and checked by decoding each address in both. C and assembly (the .S files go through the C preprocessor).
#pragma once

#if KSP_CHROMA
// the firmware's functions
#define FW_SET_MODE     0x0802BFE8      // FUN_0802B8D0(seq, mode, t, bank)
#define FW_SHOW_MODE    0x0804D100      // FUN_0804CA80(panel, mode, t, quiet)
#define FW_GET_MODE     0x0802BA80      // FUN_0802B55C(seq, t, bank)
#define FW_LED_SET      0x08054564      // FUN_08053EB8(leds, elem, lit, colour, phase, 0)
#define FW_PAGE_TIMED   0x0802546C      // FUN_08025160(D, fn, a, b, period, ms)
#define FW_OLED_CLEAR   0x08026632      // FUN_08026326
#define FW_OLED_TITLE   0x08026796      // FUN_0802648A
#define FW_OLED_BIG     0x080267DA      // FUN_080264CE
#define FW_OLED_DONE    0x080266B6      // FUN_080263AA
#define FW_PKT_BUILD    0x0800F8EC      // FUN_0800F6D8(buf, type, group, idx, &w)
#define FW_PKT_SEND     0x080104F4      // FUN_080102E0(port, buf)
#define FW_PKT_SEND_4   0x080104F8      //   after its `push {r4,lr}; sub sp,#0x10`
#define FW_BTN_DOWN     0x0804B2EE      // FUN_0804AC6E: mark a button down
#define FW_PRESS_EPI    0x0805099E      // FUN_080500D8's epilogue (add sp,#0x244; pop {r4-r11,pc})
#define FW_PLAY_4       0x0803A198      // FUN_08039BDC (the step player) after its push.w
#define FW_POPUP        0x080102D0      // FUN_080100BC: the stock encoder popup
#define FW_POPUP_TIMER  0x0801F964      // FUN_0801F7D8
#define FW_LED_FLUSH    0x080552D8      // FUN_08054C2C
#define FW_NOTE_ADD     0x0802FB58      // FUN_0802F404(seq, note, slot, step, t, pat, proj, 1): a note into a step
#define FW_NOTE_DEL     0x0802FD58      // FUN_0802F604(seq, slot, step, t, pat, proj, 0): a note out of a step
#define FW_FREE_SLOT    0x0802D1C8      // FUN_0802CB30(seq, step, t): the step's first free slot, or -1
#define FW_STEP_ON      0x0803311C      // FUN_08032A98(seq, on, step, pat, t, proj, lane, 0): a step's on bit
#define FW_LAST_STEP    0x0802B71C      // FUN_0802B1A8(seq, last, t, lane): the playing length (lane 0x18: a track)
// the firmware's RAM
#define RAM_SEQ_PTR     0x2000149C
#define RAM_PANEL_PTR   0x200014BC
#define RAM_DISPLAY_PTR 0x200017FC
#define RAM_MENU_PTR    0x20001514
#define RAM_LEDS_PTR    0x20001494
#define RAM_SEL_TRACK   0x200001A8
#define RAM_PROJECT     0x20004DCD
#define RAM_PATTERN     0x20004DC8      // + track
#define RAM_MIDI_BTN    0x20002D78
#define RAM_REC_ARMED   0x20002D7B
#define RAM_DRUM_LANE   0x20003DBD
#define RAM_SLOTS       0x20003DC0      // the step slot table
#define RAM_TIES        0x20002D7C      // the tie flags
#define PROJ_STRIDE     0x1000          // the sequencer object's per-project record
#else
#define FW_SET_MODE     0x0802B8D0
#define FW_SHOW_MODE    0x0804CA80
#define FW_GET_MODE     0x0802B55C
#define FW_LED_SET      0x08053EB8
#define FW_PAGE_TIMED   0x08025160
#define FW_OLED_CLEAR   0x08026326
#define FW_OLED_TITLE   0x0802648A
#define FW_OLED_BIG     0x080264CE
#define FW_OLED_DONE    0x080263AA
#define FW_PKT_BUILD    0x0800F6D8
#define FW_PKT_SEND     0x080102E0
#define FW_PKT_SEND_4   0x080102E4
#define FW_BTN_DOWN     0x0804AC6E
#define FW_PRESS_EPI    0x0805031E
#define FW_PLAY_4       0x08039BE0
#define FW_POPUP        0x080100BC
#define FW_POPUP_TIMER  0x0801F7D8
#define FW_LED_FLUSH    0x08054C2C
#define FW_NOTE_ADD     0x0802F404
#define FW_NOTE_DEL     0x0802F604
#define FW_FREE_SLOT    0x0802CB30
#define FW_STEP_ON      0x08032A98
#define FW_LAST_STEP    0x0802B1A8
#define RAM_SEQ_PTR     0x20001498
#define RAM_PANEL_PTR   0x200014B8
#define RAM_DISPLAY_PTR 0x200017F8
#define RAM_MENU_PTR    0x20001510
#define RAM_LEDS_PTR    0x20001490
#define RAM_SEL_TRACK   0x200001A4
#define RAM_PROJECT     0x20004DAD
#define RAM_PATTERN     0x20004DA8
#define RAM_MIDI_BTN    0x20002D58
#define RAM_REC_ARMED   0x20002D5B
#define RAM_DRUM_LANE   0x20003D9D
#define RAM_SLOTS       0x20003DA0
#define RAM_TIES        0x20002D5C
#define PROJ_STRIDE     0xFEC
#endif
