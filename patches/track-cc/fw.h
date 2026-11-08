// track-cc - where main firmware 2.5.20 keeps what track-cc.c and hooks.S use, per model (the Chroma has its own
// build). C and assembly (the .S file goes through the C preprocessor). The comments name the standard build's
// functions; docs/TRACK_CC.md has the map.
#pragma once

#if KSP_CHROMA
// return points in the stock code
#define FW_UP_RESUME    0x08012D30      // knob up: the `beq.w` to the Global fallback
#define FW_DOWN_RESUME  0x080141FC      // knob down: the same
#define FW_PC_BLOCK     0x08035C0C      // FUN_08035404: the block that sends the pattern's Program Change
#define FW_MERGE        0x08035B8E      // FUN_08035404: where that block and the no-PC path meet
#define FW_MERGE_4      0x08035B92      //   after the two instructions the hook replaced
#define FW_PROG_4       0x08047C30      // FUN_080475D8 (erase + program a sector from staging) after its first two
#define FW_LOAD_4       0x08046DBC      // FUN_08046858 (project load NOR -> SDRAM) after its push.w
// the firmware's functions
#define FW_MSG_CC       0x08018002      // FUN_08017EA6(ch, cc, val): the packed `Bn cc val`
#define FW_MIDI_SEND    0x0801A83C      // FUN_0801A6E0(ports, msg, port): send to the knob's port(s)
#define FW_DRUM         0x0802BA80      // FUN_0802B55C(seq, t, project): the track's Drum mode flag
#define FW_NOR_READ     0x0803FCD4      // FUN_0803F774(nor, src, dst, halfwords): reset to read mode, copy
#define FW_PROJ_SAVE    0x080482E4      // FUN_08047C84(h, src, dst): the project save
#define FW_STORE_CALL   0x08048404      // the settings-store save's call of the program function
#define FW_FFF0_CALL    0x0804843C      // the 0xFFF0 block's call of the program function
// the firmware's RAM
#define RAM_MODE        0x200001A8      // the selected-track byte: 0..3 a track, 4 = Control mode
#define RAM_TRACK       0x20004DD7      // the current track 0..3
#define RAM_PROJECT     0x20004DCD      // the current project 0..15
#define RAM_PATTERN     0x20004DC8      // + track: the track's current pattern
#define RAM_SEQ_PTR     0x2000149C      // the sequencer object
#define RAM_SETTINGS    0x20000878      // setting id: *(RAM_SETTINGS + 4 + id)
#define RAM_PORTS_PTR   0x20001804      // the MIDI out object, 1st argument of FW_MIDI_SEND
#define RAM_NOR         0x20001824      // the NOR handle
#else
#define FW_UP_RESUME    0x08012B1C
#define FW_DOWN_RESUME  0x08013FE8
#define FW_PC_BLOCK     0x080355E0
#define FW_MERGE        0x08035562
#define FW_MERGE_4      0x08035566
#define FW_PROG_4       0x080475DC
#define FW_LOAD_4       0x0804685C
#define FW_MSG_CC       0x08017EA6
#define FW_MIDI_SEND    0x0801A6E0
#define FW_DRUM         0x0802B55C
#define FW_NOR_READ     0x0803F774
#define FW_PROJ_SAVE    0x08047C84
#define FW_STORE_CALL   0x08047D88
#define FW_FFF0_CALL    0x08047DC0
#define RAM_MODE        0x200001A4
#define RAM_TRACK       0x20004DB7
#define RAM_PROJECT     0x20004DAD
#define RAM_PATTERN     0x20004DA8
#define RAM_SEQ_PTR     0x20001498
#define RAM_SETTINGS    0x20000874
#define RAM_PORTS_PTR   0x20001800
#define RAM_NOR         0x20001820
#endif

// the same on both models
#define NOR_BASE        0x60000000u     // project p: sector A at NOR_BASE + p * 0x20000, sector B 0x10000 above
#define STAGING         0xC01F0000u     // the 64 KiB image the program function is about to write
#define BLOCK_OFF       0xE000u         // our block's place in every project's sector A (never filled by the firmware)
