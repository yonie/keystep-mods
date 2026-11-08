// track-cc - in Control mode the five knobs follow the track Control mode was entered from, and remember what they
// sent for that track's pattern.
//
// 1. A knob on the Global channel sends on the channel of the track Control mode was entered from (Track 1 in Drum mode:
//    its Drum Output channel). A knob with its own channel keeps it.
// 2. The value such a knob sends is kept per project, track, pattern and knob. Whenever that pattern's sound is
//    (re)loaded - a Program Change goes out for the track, or the track switches pattern, PC or not - the kept values
//    go out right after, as the knobs send them (same CC numbers, same port(s), the track's channel), before the
//    pattern's first note. A synth reloads its stored patch on a Program Change; this puts the tweaks back.
// 3. The values are saved with the project, inside the firmware's own save of the project's first sector, and loaded
//    with it. A block that is not ours, erased or damaged loads as nothing kept.
//
// How it hooks the firmware: docs/TRACK_CC.md. Main firmware 2.5.20, both models (fw.h).
#include <stdint.h>

#include "fw.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

#define FW(addr, type) ((type)((addr) | 1u))
#define msg_cc    FW(FW_MSG_CC, u32 (*)(int ch, int cc, int val))
#define midi_send FW(FW_MIDI_SEND, void (*)(void *ports, u32 msg, int port))
#define get_drum  FW(FW_DRUM, int (*)(void *seq, int t, int project))
#define nor_read  FW(FW_NOR_READ, int (*)(void *nor, u32 src, void *dst, int halfwords))

#define MODE       (*(volatile u8 *)RAM_MODE)
#define TRACK      (*(volatile u8 *)RAM_TRACK)
#define PROJECT    (*(volatile u8 *)RAM_PROJECT)
#define PATTERN(t) (*(volatile u8 *)(RAM_PATTERN + (t)))
#define SEQ        (*(void **)RAM_SEQ_PTR)
#define PORTS      (*(void **)RAM_PORTS_PTR)
#define SETTING(id) (*(volatile u8 *)(RAM_SETTINGS + 4 + (id)))

#define ID_KNOB_CC   0x1C        // + knob: its CC number
#define ID_KNOB_PORT 0x21        // + knob: its port(s)
#define ID_KNOB_CH   0x26        // + knob: its channel, 0x7F = Global
#define ID_TRACK_CH  0x4B        // + track: MIDI Channel OUT
#define ID_DRUM_CH   0x4F        // Track 1's Drum Output channel
#define GLOBAL       0x7F

#define KNOBS     5
#define PER_PROJ  (4 * 16 * KNOBS)          // 4 tracks x 16 patterns x 5 knobs = 320 bytes
#define NONE      0xFF                      // nothing kept

// the block in NOR, at BLOCK_OFF in every project's sector A: 332 bytes (166 halfwords)
#define B_MAGIC   0x4B434354u               // "TCCK"
#define B_VERSION 1
#define B_HEAD    8                         // magic, version (u16), length (u16)
#define B_SIZE    (B_HEAD + PER_PROJ + 4)   // ... data, CRC-32 of everything before it
#define LOADED    0x4C434354u               // "TCCL": this project's RAM was filled by tcc_load

// RAM: in the SDRAM block patch.json places us at; not cleared at boot, so a project's values are trusted only
// once tcc_load has marked it
static struct {
    u8 vals[16][PER_PROJ] __attribute__((aligned(4)));
    u32 loaded[16];
    u16 buf[(B_SIZE + 1) / 2];              // a block read from NOR
    u8 in_switch;                           // the pattern switch sends its PC: the switch sends the values
} S;

static void fill(volatile u32 *d, u32 v, int n)    // n bytes, a multiple of 4 (volatile: no library memset)
{
    for (n /= 4; n--;)
        *d++ = v;
}

static void copy(volatile u8 *d, const volatile u8 *s, int n)
{
    while (n--)
        *d++ = *s++;
}

static u32 crc32(const volatile u8 *p, int n)       // CRC-32 (IEEE), four bits at a time
{
    static const u32 T[16] = {
        0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
        0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C, 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C};
    u32 c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        c = (c >> 4) ^ T[c & 15];
        c = (c >> 4) ^ T[c & 15];
    }
    return ~c;
}

static int channel(int t)                   // the channel track t plays on, as Control mode follows it
{
    int id = ID_TRACK_CH + t;
    if (t == 0 && get_drum(SEQ, 0, PROJECT))
        id = ID_DRUM_CH;
    return SETTING(id) & 15;
}

static u8 *slot(int p, int t, int pat)
{
    if (p > 15 || t > 3 || pat > 15 || S.loaded[p] != LOADED)
        return 0;
    return &S.vals[p][(t * 16 + pat) * KNOBS];
}

// hooks.S, a knob turned in Control mode: setting = the knob's Channel, knob 0..4, value = what it sends. Returns the
// channel to send on, or GLOBAL for the stock Global fallback.
int tcc_knob(int setting, int knob, int value)
{
    if (setting != GLOBAL || MODE != 4)
        return setting;
    int t = TRACK;
    if (t > 3)
        return setting;
    u8 *v = slot(PROJECT, t, PATTERN(t));
    if (v && knob < KNOBS)
        v[knob] = (u8)(value & 0x7F);
    return channel(t);
}

static void send_kept(int t, int pat)       // the kept values of track t's pattern, as the knobs send them
{
    u8 *v = slot(PROJECT, t, pat);
    if (!v)
        return;
    int ch = -1;
    for (int k = 0; k < KNOBS; k++) {
        if (v[k] > 0x7F || SETTING(ID_KNOB_CH + k) != GLOBAL)
            continue;
        if (ch < 0)
            ch = channel(t);
        midi_send(PORTS, msg_cc(ch, SETTING(ID_KNOB_CC + k), v[k]) & 0xFFFFFF, SETTING(ID_KNOB_PORT + k));
    }
}

// hooks.S, the Program Change sender has just sent track t's PC
void tcc_after_pc(int t)
{
    if (!S.in_switch && t < 4)
        send_kept(t, PATTERN(t));
}

// hooks.S, a pattern switch (FUN_08035404 with its send flag) is about to send the pattern's PC block
void tcc_switch_begin(void)
{
    S.in_switch = 1;
}

// hooks.S, the switch is past its PC block (sent, skipped, or off): the new pattern's values
void tcc_after_switch(int t, int pat)
{
    if (!S.in_switch)
        return;
    S.in_switch = 0;
    send_kept(t, pat);
}

// hooks.S, the program function is about to erase `sector` and program it from the staging buffer
void tcc_save(u32 sector, u32 ret)
{
    u32 call = (ret & ~1u) - 4;
    if (call == FW_STORE_CALL || call == FW_FFF0_CALL)
        return;                             // the settings store, not a project save: our bytes stay as read back
    u32 off = sector - NOR_BASE;
    if (off >= 0x200000u || (off & 0x1FFFFu))
        return;                             // not a sector A
    int p = off >> 17;
    if (S.loaded[p] != LOADED)
        return;                             // RAM not ours: keep what the read-back holds
    volatile u8 *b = (volatile u8 *)(STAGING + BLOCK_OFF);
    u32 magic = B_MAGIC;
    copy(b, (const volatile u8 *)&magic, 4);
    b[4] = B_VERSION;
    b[5] = 0;
    b[6] = PER_PROJ & 0xFF;
    b[7] = PER_PROJ >> 8;
    copy(b + B_HEAD, S.vals[p], PER_PROJ);
    u32 crc = crc32(b, B_HEAD + PER_PROJ);
    copy(b + B_HEAD + PER_PROJ, (const volatile u8 *)&crc, 4);
}

// hooks.S, the firmware has loaded project src from NOR into project dst's SDRAM
void tcc_load(int src, int dst)
{
    S.in_switch = 0;
    if (src > 15 || dst > 15)
        return;
    S.loaded[dst] = 0;
    u32 src_at = NOR_BASE + ((u32)src << 17) + BLOCK_OFF;
    const volatile u8 *b = (const volatile u8 *)S.buf;
    if (nor_read((void *)RAM_NOR, src_at, S.buf, B_HEAD / 2))
        return;                             // the chip was busy: nothing trusted, nothing recorded or saved
    u32 magic = b[0] | b[1] << 8 | b[2] << 16 | (u32)b[3] << 24;
    int ours = magic == B_MAGIC && b[4] == B_VERSION && b[5] == 0 && (b[6] | b[7] << 8) == PER_PROJ;
    if (ours) {                             // the rest only when the header is ours (a boot reads all 16)
        if (nor_read((void *)RAM_NOR, src_at + B_HEAD, S.buf + B_HEAD / 2, (B_SIZE - B_HEAD) / 2))
            return;
        u32 crc = b[B_HEAD + PER_PROJ] | b[B_HEAD + PER_PROJ + 1] << 8 | b[B_HEAD + PER_PROJ + 2] << 16 |
                  (u32)b[B_HEAD + PER_PROJ + 3] << 24;
        ours = crc == crc32(b, B_HEAD + PER_PROJ);
    }
    if (ours)
        copy(S.vals[dst], b + B_HEAD, PER_PROJ);
    else                                    // a fresh unit, an erased project, or not our block: nothing kept
        fill((volatile u32 *)S.vals[dst], 0xFFFFFFFFu, PER_PROJ);
    S.loaded[dst] = LOADED;
}

// hooks.S, Copy Project is about to save project src into dst: the values go with it
void tcc_copy(int src, int dst)
{
    if (src > 15 || dst > 15 || src == dst || S.loaded[src] != LOADED)
        return;
    copy(S.vals[dst], S.vals[src], PER_PROJ);
    S.loaded[dst] = LOADED;
}
