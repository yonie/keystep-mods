// generative - generative tracks for the KeyStep Pro (main firmware 2.5.20).
//
// Hold Track 2, 3 or 4's Seq button and press its Arp button: the track goes into Flux - a loop of random notes that
// repeats, and changes a little on every pass as much as you let it (a Turing machine). Hold Track 1's Seq and press
// Drum: it goes into Pulse - every drum lane (keyboard key) spreads its own number of pulses evenly over its own number
// of steps (Euclidean rhythms); a key picks the lane the knobs set. Seq or Arp/Drum on its own takes the track back; its pattern was never touched.
//
//   encoder     Flux (Tracks 2..4)                      Pulse (Track 1, the drum lane picked with a key)
//   Pitch       Range: note spread, 1..36 semitones      Dynamics: how much each hit's velocity varies, 0..100 %
//   Gate        Loop Length: 1..16 steps                 Steps: the lane's loop, 1..16
//   Velocity    Density: steps that play, 0..100 %      Pulses: 0..Steps, spread evenly
//   Time Shift  Change: per pass, 0 (locked)..100 %     Rotate: shift the pulses, 0..Steps-1
//   Randomness  Riff: any turn makes a new loop         Chance: each pulse plays with this chance, 0..100 %
//   Last Step + Step N: Loop Length (Flux) / the lane's Steps (Pulse) = N; the stored pattern's length stays as it is
//   Record (stock): while armed, every step played is written into the track's pattern as the playhead passes
//
// Both follow the KeyStep's own Shift + keyboard settings: scale and root (the firmware quantises the notes), time
// division and triplets (the step clock) and the playback direction Fwd / Rand / Walk (read here), and start again
// from step 1 on Play.
//
// How a generated note is played: for the one step being played, the step player FUN_08039BDC is shown a step
// that holds our note (the track's slot table blanked but for that step, one pool entry, its step-on bit and its 4-bit step mask, plus the
// tie flag of that slot), runs exactly as for a stored note - channel, gate length, note-off, USB, CV/gate, DIN,
// mute, the KeyStep's own transpose and scale - and every byte is put back before it returns. Mapping and
// addresses: docs/GENERATIVE.md.
//
// Everything lives in a block of SDRAM the firmware never uses (patch.json "place"), not cleared at boot: nothing is trusted until init()
// has written the marker. Nothing is saved: generative tracks are gone at power-off, by design (projects must stay
// exactly what MIDI Control Center syncs).
#include <stdint.h>

#include "fw.h"                                         // the firmware's addresses, per model

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

// ---- the firmware ---------------------------------------------------------------------------------------------------
#define FW(addr, type) ((type)((addr) | 1u))
#define SEQ        (*(u8 **)RAM_SEQ_PTR)     // the sequencer object
#define PANEL      (*(u8 **)RAM_PANEL_PTR)     // the button handler's panel object
#define DISPLAY    (*(void **)RAM_DISPLAY_PTR)   // the OLED page object D
#define MENU       (*(u8 **)RAM_MENU_PTR)     // the screen manager M (+0x0E: a menu is open)
#define LEDS       (*(u8 **)RAM_LEDS_PTR)     // the element LED object
#define SEL_TRACK  (*(volatile u8 *)RAM_SEL_TRACK)
#define PROJECT    (*(volatile u8 *)RAM_PROJECT)
#define PATTERN(t) (*(volatile u8 *)(RAM_PATTERN + (t)))
#define MIDI_BTN   (*(volatile u8 *)RAM_MIDI_BTN)   // buttons are sent as MIDI instead of acting
#define REC_ARMED  (*(volatile u8 *)RAM_REC_ARMED)
#define DRUM_LANE  (*(volatile u8 *)RAM_DRUM_LANE)   // the drum lane picked with a key (0..23)
#define PLAYING(seq) ((seq)[0x0C])                 // 1 while the transport runs
#define TIM2_CNT   (*(volatile u32 *)0x40000024u)
#define SYST_CVR   (*(volatile u32 *)0xE000E018u)

#define set_mode   FW(FW_SET_MODE, void (*)(u8 *seq, int mode, int t, int bank))
#define show_mode  FW(FW_SHOW_MODE, void (*)(u8 *panel, int mode, int t, int quiet))
#define get_mode   FW(FW_GET_MODE, int (*)(u8 *seq, int t, int bank))
#define led_set    FW(FW_LED_SET, void (*)(u8 *leds, int elem, int lit, int colour, int phase, int x))
#define page_timed FW(FW_PAGE_TIMED, void (*)(void *d, void (*fn)(void *), int a, int b, int period, int ms))
#define oled_clear FW(FW_OLED_CLEAR, void (*)(void *d))
#define oled_title FW(FW_OLED_TITLE, void (*)(void *d, const char *s))
#define oled_big   FW(FW_OLED_BIG, void (*)(void *d, const char *s))
#define oled_done  FW(FW_OLED_DONE, void (*)(void *d))
#define pkt_build  FW(FW_PKT_BUILD, void (*)(u8 *buf, int type, int group, int idx, u32 *w))
#define pkt_send   FW(FW_PKT_SEND, void (*)(int port, u8 *buf))
#define note_add   FW(FW_NOTE_ADD, int (*)(u8 *seq, int note, int slot, int step, int t, int pat, int proj, int init))
#define note_del   FW(FW_NOTE_DEL, int (*)(u8 *seq, int slot, int step, int t, int pat, int proj, int pack))
#define free_slot  FW(FW_FREE_SLOT, int (*)(u8 *seq, int step, int t))
#define step_on    FW(FW_STEP_ON, int (*)(u8 *seq, int on, int step, int pat, int t, int proj, int lane, int check))
#define last_step  FW(FW_LAST_STEP, void (*)(u8 *seq, int last, int t, int lane))

void gen_play_orig(u8 *seq, int t);                       // hooks.S: the stock step player

// ---- state (SDRAM) ----------------------------------------------------------------------------------------------
#define MAGIC 0x47454E32u                                 // "GEN2"
#define NT 4

typedef struct {
    u8 active, len, density, range, change, pos, cur, flash;   // active: 1 Flux, 2 Pulse
    u8 note[16], on[16], vel[16];
} track_t;

#define NL 24                                             // drum lanes
typedef struct {
    u8 steps, pulses, rot, chance, pos, cur, fired, dyn;     // fired: the velocity it hit with (0: none)
} lane_t;

static struct {
    u32 magic;
    u32 rng;
    track_t trk[NT];
    u8 popup_knob, popup_dirty, own_tx, shown_track;       // shown_track: 0xFF none
    u8 sent[16][3];                                        // the step colours last sent
    u8 sent_valid, was_playing, euc_init;
    lane_t lane[NL];
    // while a Pulse step plays: what it borrowed (here, not on the interrupt's stack)
    u8 slot_save[64 * 16], save_pool[16 * 5], save_nib[8], save_tie[16], who[16];
    u32 save_on[16][2];
    // Record (the stock Record armed): what each step played, written into the pattern by the main loop
    // (lock-free: the interrupt writes the data, then the flag; the main loop clears the flag, then reads the data)
    u8 rf_flag[NT][16], rf_note[NT][16], rf_vel[NT][16];   // Flux: played, its note (0xFF: a rest), its velocity
    u8 rl_flag[NL][16], rl_hit[NL][16];                    // Pulse: per lane and step, played and its velocity (0: none)
    u8 rec_wrote[NT];                                      // written since entering: leaving lines the pattern up
} g;

static void init(void)
{
    if (g.magic == MAGIC)
        return;
    u8 *p = (u8 *)&g;
    for (unsigned i = 0; i < sizeof g; i++)
        p[i] = 0;
    g.rng = 0x9E3779B9u ^ TIM2_CNT ^ SYST_CVR;
    g.shown_track = 0xFF;
    g.magic = MAGIC;
}

static u32 rnd(u32 n)                                     // 0 .. n-1 (xorshift32)
{
    u32 x = g.rng ? g.rng : 0x2545F491u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    g.rng = x;
    return n ? x % n : 0;
}

static void roll_step(track_t *s, int i)
{
    s->note[i] = (u8)rnd(s->range);
    s->on[i] = rnd(100) < s->density;
    s->vel[i] = (u8)(84 + rnd(40));
}

static void roll_all(track_t *s)
{
    for (int i = 0; i < 16; i++)
        roll_step(s, i);
}

static int pulse_on(const lane_t *l, int i)                // pulses spread evenly over the steps, rotated
{
    int n = l->steps ? l->steps : 16, k = l->pulses;
    if (k <= 0)
        return 0;
    int j = (i - l->rot % n + n) % n;
    return (j * k) % n < k;
}

static int next_pos(int dir, int i, int len)              // the playback direction: Fwd / Rand / Walk
{
    if (dir == 1)                                           // Rand: any step of the loop
        return (int)rnd((u32)len);
    if (dir == 2) {                                         // Walk: next 50 %, previous 25 %, the same 25 %
        u32 r = rnd(4);
        return r < 2 ? (i + 1) % len : r == 2 ? (i + len - 1) % len : i;
    }
    return (i + 1) % len;
}

static void restart(void)                                 // Play: every loop from its first step
{
    for (int t = 0; t < NT; t++)
        g.trk[t].pos = 0;
    for (int l = 0; l < NL; l++)
        g.lane[l].pos = 0;
}

int gen_is_active(int t)
{
    return t >= 0 && t < NT && g.magic == MAGIC && g.trk[t].active;
}

// ---- entering and leaving -----------------------------------------------------------------------------------------
static void mark_steps_dirty(void)                          // the stock flush resends its own step colours
{
    u8 *l = LEDS;
    for (int e = 0x35; e <= 0x44; e++)
        l[0x184 + e] = 1;
}

static void pulse_defaults(void)                          // a plain beat to start from: kick, snare, closed hat
{
    for (int l = 0; l < NL; l++) {
        lane_t *e = &g.lane[l];
        e->steps = 16; e->pulses = 0; e->rot = 0; e->chance = 100; e->cur = 0xFF; e->dyn = 0;
    }
    g.lane[0].pulses = 4;                                   // 36
    g.lane[2].pulses = 2; g.lane[2].rot = 4;                // 38: the backbeat
    g.lane[6].pulses = 8;                                   // 42
    g.euc_init = 1;
}

static void enter(int t)
{
    track_t *s = &g.trk[t];
    int mode = t == 0;                                      // Pulse plays from the Drum player, Flux from Seq
    if (mode) {
        if (!g.euc_init)
            pulse_defaults();
        for (int l = 0; l < NL; l++)
            g.lane[l].pos = 0;
    } else {
        if (!s->len) {                                      // first time: the defaults
            s->len = 8; s->density = 75; s->range = 12; s->change = 10;
        }
        roll_all(s);
    }
    s->pos = 0; s->cur = 0xFF; s->flash = 0;
    g.rec_wrote[t] = 0;
    for (int i = 0; i < 16; i++) {
        g.rf_flag[t][i] = 0;
        for (int l = 0; l < NL; l++)
            g.rl_flag[l][i] = 0;
    }
    u8 *seq = SEQ;
    if (get_mode(seq, t, PROJECT) != mode) {
        set_mode(seq, mode, t, PROJECT);
        show_mode(PANEL, mode, t, 0);
    }
    s->active = (u8)(mode ? 2 : 1);
    LEDS[0x11 + t] = 1;                                     // resend the track display (gen_tx_filter blanks it)
    g.sent_valid = 0;
}

#define BARRIER() __asm volatile("" ::: "memory")

static int take(u8 *flag)                                  // a step the interrupt marked: unmark it, then read it
{
    if (!*(volatile u8 *)flag)
        return 0;
    *(volatile u8 *)flag = 0;
    BARRIER();
    return 1;
}

// While the stock Record is armed, every step a generative track plays is marked by the step player (interrupt) and
// written here, in the main loop, into the pattern the track is on, with the firmware's own step editing: the step's
// old notes go and what was heard goes in (a rest leaves the step empty). The pattern's length follows the loop.
static void rec_flux(int t)
{
    u8 *seq = SEQ;
    int proj = PROJECT, pat = PATTERN(t);
    track_t *s = &g.trk[t];
    u8 *table = (u8 *)(RAM_SLOTS + (u32)t * 0x400);
    u8 *pool = (u8 *)(0xC0000000u + (u32)proj * 0xF000 + (u32)t * 0x3C00 + (u32)pat * 0x3C0);
    u8 *rec = seq + proj * PROJ_STRIDE + t * 0x140 + pat * 0x14 + 0x930;
    u32 *onw = (u32 *)(rec + 0xC);
    u32 *nib = (u32 *)(0xC0104000u + (u32)(pat + (t + proj * 5) * 0x10) * 0x20);
    int len = s->len ? s->len : 8;
    for (int i = 0; i < 16; i++) {
        if (!take(&g.rf_flag[t][i]))
            continue;
        if (rec[8] != len - 1) {
            rec[8] = (u8)(len - 1);                         // the pattern's last step
            last_step(seq, len - 1, t, 0x18);
        }
        for (int k = 15; k >= 0; k--)
            if (table[i * 16 + k] != 0xFF)
                note_del(seq, k, i, t, pat, proj, 0);
        onw[0] &= ~(1u << i);
        g.rec_wrote[t] = 1;
        int note = g.rf_note[t][i];
        if (note == 0xFF || !note_add(seq, note, 0, i, t, pat, proj, 1))
            continue;
        u8 *p = pool + table[i * 16] * 5;
        p[1] = 7; p[2] = g.rf_vel[t][i]; p[3] = 0x31; p[4] = 100;
        nib[i / 8] |= 0xFu << ((i & 7) * 4);                // no ratchet
        step_on(seq, 1, i, pat, t, proj, 0, 0);
    }
}

static void rec_pulse(void)
{
    u8 *seq = SEQ;
    int proj = PROJECT, pat = PATTERN(0);
    u8 *rec = seq + proj * PROJ_STRIDE + pat * 0x20 + 0xE30;
    u8 *table = (u8 *)RAM_SLOTS;
    u8 *pool = (u8 *)(0xC0114000u + (u32)proj * 0x3C00 + (u32)pat * 0x3C0);
    u32 *on = (u32 *)(0xC0150000u + (u32)(pat * 0x18 + proj * 0x180) * 8);
    u8 *nib = (u8 *)(0xC010E000u + (u32)proj * 0x600 + (u32)pat * 0x60);
    for (int l = 0; l < NL; l++) {
        lane_t *e = &g.lane[l];
        int len = e->steps ? e->steps : 16;
        for (int i = 0; i < 16; i++) {
            if (!take(&g.rl_flag[l][i]))
                continue;
            if (!(rec[0x1F] & 4) || rec[7 + l] != len - 1) {
                rec[7 + l] = (u8)(len - 1);                 // the lane's own last step
                last_step(seq, len - 1, 0, l);
                rec[0x1F] |= 4;                             // the lanes keep their own lengths
            }
            for (int k = 15; k >= 0; k--) {                 // this lane's hits on the step go
                int x = table[i * 16 + k];
                if (x != 0xFF && pool[x * 5] == l)
                    note_del(seq, k, i, 0, pat, proj, 0);
            }
            on[2 * l] &= ~(1u << i);
            g.rec_wrote[0] = 1;
            if (!g.rl_hit[l][i])
                continue;
            int slot = free_slot(seq, i, 0);
            if (slot < 0 || !note_add(seq, l, slot, i, 0, pat, proj, 1))
                continue;                                   // the pattern is full
            int x = table[i * 16 + slot];
            u8 *p = pool + x * 5;
            p[1] = 7; p[2] = g.rl_hit[l][i]; p[3] = 0x31; p[4] = 100;
            nib[x / 2] |= (u8)(0xF << ((x & 1) * 4));      // no ratchet
            step_on(seq, 1, i, pat, 0, proj, l, 0);
        }
    }
}

static void rec_flush(int t)
{
    if (g.trk[t].active == 2)
        rec_pulse();
    else if (g.trk[t].active == 1)
        rec_flux(t);
}

static void leave(int t)
{
    rec_flush(t);
    track_t *s = &g.trk[t];
    u8 *seq = SEQ;
    if (g.rec_wrote[t]) {                                   // what was recorded plays on from the same step
        if (s->active == 2)
            for (int l = 0; l < NL; l++) {
                lane_t *e = &g.lane[l];
                seq[0x1D3 + l] = (u8)(e->cur < (e->steps ? e->steps : 16) ? e->cur : 0);
            }
        else
            seq[0x180 + t] = (u8)(s->cur < s->len ? s->cur : 0);
        g.rec_wrote[t] = 0;
    }
    s->active = 0;
    LEDS[0x11 + t] = 1;                                     // the pattern number comes back
    mark_steps_dirty();
    g.sent_valid = 0;
}

static void popup(int knob);

static void set_steps(int t, int n)                         // Flux: Loop Length; Pulse: the picked lane's Steps
{
    track_t *s = &g.trk[t];
    if (s->active == 2) {
        lane_t *e = &g.lane[DRUM_LANE < NL ? DRUM_LANE : 0];
        e->steps = (u8)n;
        if (e->pulses > n) e->pulses = (u8)n;
        e->rot = (u8)(e->rot % n);
    } else {
        s->len = (u8)n;
        s->pos = (u8)(s->pos % n);
    }
}

// FUN_080500D8's first call, for every press: 1 = taken here (the stock handler is skipped)
int gen_on_press(int code, u8 *panel)
{
    init();
    if (panel[3] || MIDI_BTN)
        return 0;
    int sel = SEL_TRACK;
    if (sel < NT && g.trk[sel].active && !MENU[0x0E]) {   // the stock pattern's length is not ours to change
        if (code >= 0x3F && code <= 0x42)                   // the step pages 16..64: a loop is 16 steps at most
            return 1;
        if (code == 0x3E) {                                 // Last Step: show the loop's length
            popup(1);
            return 1;
        }
        if (code >= 0x2E && code <= 0x3D && panel[0x6C + 0x3E * 3 + 2] == 0) {   // Last Step + Step N: N steps
            set_steps(sel, code - 0x2E + 1);
            popup(1);
            return 1;
        }
        if (code >= 0x2E && code <= 0x3D) {                 // a step: Flux turns it on or off (the stored
            track_t *s = &g.trk[sel];                       // pattern is never edited from here)
            int i = code - 0x2E;
            if (s->active == 1 && i < s->len) {
                s->on[i] ^= 1;
                g.sent_valid = 0;
            }
            panel[0x6C + code * 3 + 2] = 1;                 // not held: a key now must not step-enter a note
            return 1;
        }
    }
    if (code == 0x04 && panel[0x6C + 0x01 * 3 + 2] == 0)    // Shift + Play: the sequence restarts, so do the loops
        g.was_playing = 0;
    int d = code - 0x14;
    if (d < 0 || d > 0x13)
        return 0;
    int t = d / 6, k = d % 6;                               // the chord: Seq + Arp/Drum of a track
    if (k > 1 || t >= NT)
        return 0;
    if (t == 0 && REC_ARMED)
        return 0;
    int partner = code ^ 1;                                 // Seq <-> Arp/Drum of the same track
    int partner_held = panel[0x6C + partner * 3 + 2] == 0;
    if (partner_held) {
        if (!g.trk[t].active)
            enter(t);
        return 1;
    }
    if (g.trk[t].active)
        leave(t);                                           // and the stock handler switches to that mode
    return 0;
}

// ---- playing a step (TIM2 compare interrupt) ------------------------------------------------------------------------
static void flux_play(u8 *seq, int t)
{
    track_t *s = &g.trk[t];
    int len = s->len ? s->len : 8;
    int i = s->pos % len;
    if (s->change && rnd(100) < s->change) {
        roll_step(s, i);
        s->flash = (u8)(i + 1);
    }
    s->cur = (u8)i;
    if (REC_ARMED) {                                        // Record: this step is written into the pattern
        g.rf_note[t][i] = s->on[i] ? (u8)(60 - s->range / 2 + s->note[i]) : 0xFF;
        g.rf_vel[t][i] = s->vel[i];
        BARRIER();
        g.rf_flag[t][i] = 1;
    }

    int step = seq[0x180 + t];
    int proj = PROJECT, pat = PATTERN(t);
    // the pattern's playback direction (Shift + Fwd / Rand / Walk): bits 3..4 of its settings byte
    int dir = (seq[proj * PROJ_STRIDE + t * 0x140 + pat * 0x14 + 0x939] >> 3) & 3;
    s->pos = (u8)next_pos(dir, i, len);
    // the track's whole slot table is blanked, not just this step's row: the player's look-ahead also plays the next
    // step's notes that are nudged ahead of it (Time Shift)
    u8 *table = (u8 *)(RAM_SLOTS + (u32)t * 0x400);
    u8 *slots = table + step * 0x10;
    u8 *pool = (u8 *)(0xC0000000u + (u32)proj * 0xF000 + (u32)t * 0x3C00 + (u32)pat * 0x3C0);   // entry 0
    u32 *onw = (u32 *)(seq + proj * PROJ_STRIDE + t * 0x140 + pat * 0x14 + 0x93C + (step >= 32 ? 4 : 0));
    u32 *nib = (u32 *)(0xC0104000u + (u32)(pat + (t + proj * 5) * 0x10) * 0x20 + (u32)(step / 8) * 4);
    u8 *tie = (u8 *)(RAM_TIES + (u32)t * 0xC0);         // the tie flag of slot 0
    u8 save_pool[5], save_tie = *tie;
    u32 save_on = *onw, save_nib = *nib, bit = 1u << (step & 31);
    for (int k = 0; k < 64 * 16; k++) {
        g.slot_save[k] = table[k];
        table[k] = 0xFF;
    }
    for (int k = 0; k < 5; k++)
        save_pool[k] = pool[k];

    if (s->on[i]) {
        int base = 60 - s->range / 2;
        slots[0] = 0;
        pool[0] = (u8)(base + s->note[i]);
        pool[1] = 7;                                        // gate: 50 %
        pool[2] = s->vel[i];
        pool[3] = 0x31;                                     // no micro-offset
        pool[4] = 100;                                      // probability
        *onw = save_on | bit;
        *nib = save_nib | (0xFu << ((step & 7) * 4));
        *tie = 0;
    } else {
        *onw = save_on & ~bit;                              // a rest
    }

    gen_play_orig(seq, t);

    for (int k = 0; k < 64 * 16; k++)
        table[k] = g.slot_save[k];
    for (int k = 0; k < 5; k++)
        pool[k] = save_pool[k];
    *onw = save_on;
    *nib = save_nib;
    *tie = save_tie;
}

// Track 1 in Drum mode. The Drum player walks lanes 0..23, each from its own playhead row of the slot table, and
// plays the pool entries in that row whose lane is that lane (when its step-on bit, tie flag, probability and ratchet
// nibble allow it). For one step the whole slot table is blanked and each lane that plays gets pool entry j in its
// row; afterwards every byte is put back.
static void pulse_play(u8 *seq)
{
    int proj = PROJECT, pat = PATTERN(0);
    u8 *rec = seq + proj * PROJ_STRIDE + pat * 0x20 + 0xE36;       // the drum pattern record
    int dir = (rec[0x19] >> 6) & 3;                         // Drum mode's own Fwd / Rand / Walk
    u8 *head = seq + (dir ? 0x1EB : 0x1D3);                 // the per-lane playheads (Rand/Walk keep their own)
    u8 *slots = (u8 *)RAM_SLOTS;                          // Track 1's rows, 64 x 16
    u8 *pool = (u8 *)(0xC0114000u + (u32)proj * 0x3C00 + (u32)pat * 0x3C0);
    u8 *nib = (u8 *)(0xC010E000u + (u32)proj * 0x600 + (u32)pat * 0x60);
    u8 *tie = (u8 *)RAM_TIES;
    u32 *on = (u32 *)(0xC0150000u + (u32)(pat * 0x18 + proj * 0x180) * 8);   // lane l: on[2l], on[2l+1]
    u8 *save_pool = g.save_pool, *save_nib = g.save_nib, *save_tie = g.save_tie, *who = g.who;
    u32 (*save_on)[2] = g.save_on;
    int n = 0;

    for (int l = 0; l < NL; l++) {                          // the lanes that play on this step
        lane_t *e = &g.lane[l];
        int len = e->steps ? e->steps : 16;
        int i = e->pos % len;
        e->cur = (u8)i;
        e->pos = (u8)next_pos(dir, i, len);
        e->fired = 0;
        if (n < 16 && pulse_on(e, i) && rnd(100) < e->chance) {
            who[n++] = (u8)l;
            e->fired = 100;                                 // Dynamics: a random velocity, 100 - 70 % .. 100 + 27 %
            if (e->dyn) {
                int lo = 100 - e->dyn * 70 / 100, hi = 100 + e->dyn * 27 / 100;
                e->fired = (u8)(lo + (int)rnd((u32)(hi - lo + 1)));
            }
        }
        if (REC_ARMED) {                                    // Record: what this lane did on this step
            g.rl_hit[l][i] = e->fired;
            BARRIER();
            g.rl_flag[l][i] = 1;
        }
    }
    for (int k = 0; k < 64 * 16; k++) {
        g.slot_save[k] = slots[k];
        slots[k] = 0xFF;
    }
    for (int k = 0; k < n * 5; k++)
        save_pool[k] = pool[k];
    for (int k = 0; k < 8; k++)
        save_nib[k] = nib[k];
    for (int j = 0; j < n; j++) {
        int l = who[j], row = head[l] & 0x3F;
        save_tie[j] = tie[j];
        save_on[j][0] = on[2 * l];
        save_on[j][1] = on[2 * l + 1];
        u8 *r = slots + row * 16;
        int f = 0;
        while (f < 15 && r[f] != 0xFF)
            f++;
        r[f] = (u8)j;
        u8 *p = pool + j * 5;
        p[0] = (u8)l;
        p[1] = 7;                                           // gate: 50 %
        p[2] = g.lane[l].fired;                             // velocity
        p[3] = 0x31;                                        // no micro-offset
        p[4] = 100;                                         // probability
        nib[j / 2] |= (u8)(0xF << ((j & 1) * 4));
        tie[j] = 0;
        on[2 * l + (row >= 32)] |= 1u << (row & 31);
    }

    gen_play_orig(seq, 0);

    for (int j = n - 1; j >= 0; j--) {
        int l = who[j];
        on[2 * l] = save_on[j][0];
        on[2 * l + 1] = save_on[j][1];
        tie[j] = save_tie[j];
    }
    for (int k = 0; k < 8; k++)
        nib[k] = save_nib[k];
    for (int k = 0; k < n * 5; k++)
        pool[k] = save_pool[k];
    for (int k = 0; k < 64 * 16; k++)
        slots[k] = g.slot_save[k];
}

void gen_play(u8 *seq, int t)
{
    if (!g.was_playing) {                                   // the first step after Play: every loop from its start
        restart();
        g.was_playing = 1;
    }
    if (g.trk[t].active == 2)
        pulse_play(seq);
    else
        flux_play(seq, t);
}

// ---- encoders and the OLED -------------------------------------------------------------------------------------------
static const char *const NAMES[5] = {"Range", "Loop Length", "Density", "Change", "Riff"};
static const char *const PULSE_NAMES[5] = {"Dynamics", "Steps", "Pulses", "Rotate", "Chance"};

static lane_t *sel_lane(void)
{
    int l = DRUM_LANE;
    return &g.lane[l < NL ? l : 0];
}

static void put_num(char *p, int v, const char *unit)
{
    char tmp[6];
    int n = 0;
    if (v == 0)
        tmp[n++] = '0';
    while (v > 0 && n < 5) {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (n)
        *p++ = tmp[--n];
    while (*unit)
        *p++ = *unit++;
    *p = 0;
}

static void popup_render(void *d)
{
    if (!g.popup_dirty)
        return;
    g.popup_dirty = 0;
    int t = SEL_TRACK;
    if (t >= NT)
        return;
    track_t *s = &g.trk[t];
    char v[16];
    if (s->active == 2) {
        lane_t *e = sel_lane();
        switch (g.popup_knob) {
        case 0: put_num(v, e->dyn, " %"); break;
        case 1: put_num(v, e->steps, e->steps == 1 ? " step" : " steps"); break;
        case 2: put_num(v, e->pulses, e->pulses == 1 ? " pulse" : " pulses"); break;
        case 3: put_num(v, e->rot, e->rot == 1 ? " step" : " steps"); break;
        default: put_num(v, e->chance, " %"); break;
        }
        oled_clear(d);
        oled_title(d, PULSE_NAMES[g.popup_knob]);
        oled_big(d, v);
        oled_done(d);
        return;
    }
    switch (g.popup_knob) {
    case 0: put_num(v, s->range, " semi"); break;
    case 1: put_num(v, s->len, s->len == 1 ? " step" : " steps"); break;
    case 2: put_num(v, s->density, " %"); break;
    case 3: put_num(v, s->change, " %"); break;
    default: {
        const char *m = "Turn for new!";
        int n = 0;
        while ((v[n] = m[n]) != 0)
            n++;
        break;
    }
    }
    oled_clear(d);
    oled_title(d, NAMES[g.popup_knob]);
    oled_big(d, v);
    oled_done(d);
}

static void popup(int knob)
{
    g.popup_knob = (u8)knob;
    g.popup_dirty = 1;
    page_timed(DISPLAY, popup_render, 0, 1, 0x32, 2000);
}

static int ours(int b3)
{
    init();
    if (b3 < 3 || b3 > 7 || MENU[0x0E])
        return 0;
    return gen_is_active(SEL_TRACK);
}

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// FUN_08012338, an encoder turn: 1 = taken (the stock code skips it)
int gen_turn(int b3, int up, int mag)
{
    if (!ours(b3))
        return 0;
    track_t *s = &g.trk[SEL_TRACK];
    int n = (mag ? mag : 1) * (up ? 1 : -1);
    if (s->active == 2) {
        lane_t *e = sel_lane();
        switch (b3 - 3) {
        case 0: e->dyn = (u8)clamp(e->dyn + 5 * n, 0, 100); break;
        case 1:
            e->steps = (u8)clamp(e->steps + n, 1, 16);
            if (e->pulses > e->steps) e->pulses = e->steps;
            e->rot = (u8)(e->rot % e->steps);
            break;
        case 2: e->pulses = (u8)clamp(e->pulses + n, 0, e->steps); break;
        case 3: e->rot = (u8)((e->rot + n % e->steps + e->steps) % e->steps); break;
        default: e->chance = (u8)clamp(e->chance + 5 * n, 0, 100); break;
        }
        popup(b3 - 3);
        return 1;
    }
    switch (b3 - 3) {
    case 0: s->range = (u8)clamp(s->range + n, 1, 36); break;
    case 1: s->len = (u8)clamp(s->len + n, 1, 16); s->pos = (u8)(s->pos % s->len); break;
    case 2:
        s->density = (u8)clamp(s->density + 5 * n, 0, 100);
        for (int i = 0; i < 16; i++)
            s->on[i] = rnd(100) < s->density;
        break;
    case 3: s->change = (u8)clamp(s->change + 5 * n, 0, 100); break;
    default: roll_all(s); break;
    }
    popup(b3 - 3);
    return 1;
}

int gen_touch(int b3)
{
    if (!ours(b3))
        return 0;
    popup(b3 - 3);
    return 1;
}

int gen_untouch(int b3)
{
    return ours(b3);                                        // our popup times out on its own
}

// ---- the step LEDs and the track display (main loop) -------------------------------------------------------------
static void step_colour(int k, u8 *rgb)
{
    track_t *s = &g.trk[SEL_TRACK];
    rgb[0] = rgb[1] = rgb[2] = 0;
    if (s->active == 2) {                                   // Pulse: the picked drum lane
        lane_t *e = sel_lane();
        if (k >= e->steps)
            return;
        if (k == e->cur) {
            rgb[0] = 0x7F; rgb[1] = 0x7F; rgb[2] = 0x7F;
        } else if (pulse_on(e, k)) {
            rgb[0] = 0x00; rgb[1] = 0x38; rgb[2] = 0x30;
        } else {
            rgb[0] = 0x04; rgb[1] = 0x05; rgb[2] = 0x05;
        }
        return;
    }
    if (k >= s->len)
        return;
    if (k == s->cur) {                                      // the playhead
        rgb[0] = 0x7F; rgb[1] = 0x7F; rgb[2] = 0x7F;
    } else if (s->flash == k + 1) {                         // just changed
        rgb[0] = 0x6A; rgb[1] = 0x00; rgb[2] = 0x50;
    } else if (s->on[k]) {                                  // plays
        rgb[0] = 0x00; rgb[1] = 0x38; rgb[2] = 0x30;
    } else {                                                // a rest inside the loop
        rgb[0] = 0x04; rgb[1] = 0x05; rgb[2] = 0x05;
    }
}

void gen_tick(void)
{
    init();
    if (!PLAYING(SEQ))
        g.was_playing = 0;
    for (int r = 0; r < NT; r++)
        rec_flush(r);
    int t = SEL_TRACK;
    int show = t < NT && g.trk[t].active ? t : 0xFF;
    if (show != g.shown_track) {
        if (show == 0xFF)
            mark_steps_dirty();
        g.shown_track = (u8)show;
        g.sent_valid = 0;
    }
    if (show == 0xFF)
        return;
    u8 *panel = PANEL;                                      // both mode LEDs stay lit: Seq and Arp/Drum
    for (int b = 0; b < 2; b++) {
        int elem = panel[0x6C + (0x14 + b + 6 * show) * 3];
        if (!LEDS[0x1C + 3 * elem])
            led_set(LEDS, elem, 1, 8, 0, 0);
    }
    for (int k = 0; k < 16; k++) {
        u8 rgb[3];
        step_colour(k, rgb);
        if (g.sent_valid && rgb[0] == g.sent[k][0] && rgb[1] == g.sent[k][1] && rgb[2] == g.sent[k][2])
            continue;
        u8 buf[8];
        u32 w = (u32)rgb[0] | (u32)rgb[1] << 8 | (u32)rgb[2] << 16;
        pkt_build(buf, 2, 3, k, &w);
        g.own_tx = 1;
        pkt_send(0, buf);
        g.own_tx = 0;
        g.sent[k][0] = rgb[0]; g.sent[k][1] = rgb[1]; g.sent[k][2] = rgb[2];
    }
    g.sent_valid = 1;
    if (g.trk[show].flash && g.trk[show].flash != g.trk[show].cur + 1)
        g.trk[show].flash = 0;
}

// FUN_080102E0, every packet to the UI MCU: 0 = drop it
int gen_tx_filter(u8 *buf)
{
    if (g.magic != MAGIC || g.own_tx)
        return 1;
    int type = buf[0] & 0x0F, group = buf[1], idx = buf[2];
    if (type != 2)
        return 1;
    if (group == 3 && SEL_TRACK < NT && g.trk[SEL_TRACK].active)
        return 0;                                           // stock step colours, while ours are shown
    if (group == 1 && idx < NT && g.trk[idx].active) {
        buf[3] = g.trk[idx].active == 2 ? 0xA1 : 0xA0;      // the track display: "PU" / "FL" (ui_letters.S)
        buf[4] = 0;                                         // no dots
    }
    return 1;
}
