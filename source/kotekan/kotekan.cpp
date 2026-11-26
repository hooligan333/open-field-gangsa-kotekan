// kotekan: a synth sequencer of level 5 (docs/level-5.md of open-field 0.12, base.pack 30) that weaves the two
// interlocking parts of Balinese kotekan, polos and sangsih, around the pokok tone the player holds. Its settings
// are kept with the tape through level 7 (docs/level-7.md). Names no address of the firmware and changes no place of
// it. The logic is core/kotekan_core.h; this file holds it to the base's threads. Compiled for the PC as it is, with
// OP1F_HOST (host/sim.cpp). Nothing here has run on a unit.
//
// The threads. `key` comes on whatever thread brings the key, the audio thread among them; `tick` on the sequencer
// thread; `run` on the key thread, a MIDI handler's or the start's; `input` (turns) on the key thread; `shown` and
// `changed` on the display thread outside the frame function. None of them logs, waits or calls anything but the
// level's note helpers and op1f_mask/op1f_unmask. The player (keys held, the figure, the notes sounding) is
// read and changed only inside masked sections, with no helper called inside one; the calls come after, from the
// decisions (KcActions). A stop, or the last key up (which ends every note at once), bumps the player's epoch: a tick whose notes went out after one ends them.
// The frame function reads single words only: the settings word it writes, the counts, the page's words.
//
// Nothing is divided, nothing of a runtime is called, no float, no 64-bit number, no copy or clear of a block by the
// compiler (the tree builds with -fno-tree-loop-distribute-patterns; nothing here is a struct copy).

#include "build_r6e_card.h"
#include "kotekan_core.h"

typedef unsigned int u32;

extern "C" const Op1fCard op1f_card_kotekan OP1F_NAMED("op1f_card_kotekan");
#define CARD (&op1f_card_kotekan)

enum { SYNTH = 0, SOURCES = 2, PLACES = 128, PLACE_OF_KEY = 53, ASK = 0, QUIET_FRAMES = 30 };
enum { FRAME_LEVEL_4 = 40, INPUT_LEVEL_4 = 40, TICK_LEVEL_5 = 32, KEY_LEVEL_5 = 32, KNOBS = 4 };
enum { VERSION = 1, TEXT_ROOM = 128, PERIOD_FRAMES = 512, SAID_MOST = 16 };
enum { W = 480, H = 224, LEFT = 16, RIGHT = 464, TOP = 10, COL = 116, LABEL_Y = 40, VALUE_Y = 52, SUB_Y = 70,
       LANE_X = 28, LANE_DX = 38, LANE_DX12 = 25, DOT = 20, DOT12 = 16, SANG_Y = 90, POLOS_Y = 146, ROW_H = 5, ROWS = 10, ZERO_ROW = 3,
       KEYS_X = 352, KEYS_Y = 104, WHITE_W = 16, WHITE_H = 64, BLACK_W = 10, BLACK_H = 38, HINT_Y = 206 };

static const char LOG_FILE[] = "op1f/kotekan.cpp";
static const char KEPT[] = "kotekan_settings";
static const char NAME[] = "kotekan";
static const unsigned BLUE = OP1F_COLOUR(0x3c, 0x60, 0x87);          // the knobs: blue, ochre, grey, red
static const unsigned OCHRE = OP1F_COLOUR(0x8a, 0x65, 0x3c);
static const unsigned GREY = OP1F_COLOUR(0xa2, 0xa4, 0xa6);
static const unsigned RED = OP1F_COLOUR(0xe5, 0x28, 0x17);
static const unsigned LIGHT_GREY = OP1F_COLOUR(0xcd, 0xd0, 0xc2);
static const unsigned DARK_GREY = OP1F_COLOUR(0x37, 0x3d, 0x42);
static const unsigned MID_GREY = OP1F_COLOUR(0x6a, 0x6e, 0x72);
static const unsigned BLACK_KEY = OP1F_COLOUR(0x1c, 0x1e, 0x20);

static const char* const STYLE_NAME[KC_STYLES] = { "nyog cag", "norot", "telu", "empat" };
static const char* const MAP_NAME[KC_MAPS] = { "selisir", "tembung", "sunaren", "black" };
static const char* const MAP_KEYS[KC_MAPS] = { "c d e g a", "f g a c d", "g a b d e", "c# d# f# g# a#" };
static const char* const RATE_NAME[KC_RATES] = { "2/beat", "3/beat", "4/beat", "6/beat", "8/beat" };
static const char* const PARTS_NAME[KC_PARTS] = { "both", "polos", "sangsih", "+pokok" };
static const char* const PARTS_SUB[KC_PARTS] = { "polos and sangsih", "the lower part", "the upper part", "both, pokok below" };

// ---- the player: inside masked sections only (the page's words excepted)
static KcPlayer P;
static volatile u32 live = (u32)KC_DEFAULT_STYLE | (u32)KC_DEFAULT_MAP << 4 | (u32)KC_DEFAULT_RATE << 8 | (u32)KC_DEFAULT_PARTS << 12;

// ---- counts, single words (bumped inside a masked section where two threads may bump the same one)
static volatile u32 keys_taken, notes_on, notes_off, refused, late, silences, runs_on, runs_stop;
static volatile u32 ticks, tempo, clock_from, restarts;              // tick's alone
static volatile u32 turned[KNOBS];                                    // input's alone (the key thread)
static volatile u32 shown_how, shown_where;                           // shown's alone
static volatile u32 told;                                             // changed's: one bit for each `how`

static void bump(volatile u32* count) {
    u32 was = op1f_mask();
    *count = *count + 1u;
    op1f_unmask(was);
}

// The calls a decision asks for: offs first, then ons. An on refused leaves the player's list; ons made after a stop
// or a silence (the epoch moved meanwhile) are ended again at once.
static void perform(const KcActions* a) {
    for (u32 i = 0; i < a->offs; ++i) {
        if (op1f_note_off(CARD, a->off[i], 0)) bump(&notes_off);
        else bump(&refused);
    }
    u32 made = 0;
    for (u32 i = 0; i < a->ons; ++i) {
        if (op1f_note_on(CARD, a->on[i], a->on_velocity[i], 0)) { bump(&notes_on); made |= 1u << i; continue; }
        bump(&refused);
        u32 was = op1f_mask();
        kc_refused(&P, a->epoch, a->on[i]);
        op1f_unmask(was);
    }
    if (!made) return;
    u32 was = op1f_mask();
    u32 stale = P.epoch != a->epoch;
    op1f_unmask(was);
    if (!stale) return;
    for (u32 i = 0; i < a->ons; ++i)
        if (made & (1u << i)) {
            bump(&late);
            if (op1f_note_off(CARD, a->on[i], 0)) bump(&notes_off);
        }
}

extern "C" int kotekan_key(const Op1fKey* e) OP1F_NAMED("kotekan_key");
extern "C" int kotekan_key(const Op1fKey* e) {
    if (e->bytes < (u32)KEY_LEVEL_5 || e->host != (u32)SYNTH || e->from >= (u32)SOURCES) return 0;
    u32 down = e->what == (u32)OP1F_DOWN;
    if (!down && e->what != (u32)OP1F_UP) return 0;
    int place = e->key + PLACE_OF_KEY;                                // a MIDI note may lie anywhere: bounds first
    if (place < 0 || place >= PLACES) return 0;
    if (down && (e->note > 127u || e->velocity == 0 || e->velocity > 127u)) return 0;
    KcActions a;
    u32 was = op1f_mask();
    kc_key_event(&P, (int)down, e->from, (u32)place, e->note, e->velocity, &a);   // the last key up: every note ends now
    if (down) keys_taken = keys_taken + 1u;
    else if (a.offs) silences = silences + 1u;
    op1f_unmask(was);
    perform(&a);
    return 1;
}

extern "C" void kotekan_tick(const Op1fTick* now) OP1F_NAMED("kotekan_tick");
extern "C" void kotekan_tick(const Op1fTick* now) {
    if (now->bytes < (u32)TICK_LEVEL_5 || now->host != (u32)SYNTH) return;
    int restart = now->jumped != 0u || (now->clock == (u32)OP1F_CLOCK_TAPE && ticks && clock_from != (u32)OP1F_CLOCK_TAPE);
    if (restart) restarts = restarts + 1u;                            // the tape started, or the count went back: the grid anew
    ticks = ticks + 1u;
    tempo = now->tempo;
    clock_from = now->clock;
    KcActions a;
    u32 was = op1f_mask();
    kc_tick(&P, live, now->count, now->pulse, restart, &a);
    op1f_unmask(was);
    perform(&a);
}

extern "C" void kotekan_run(unsigned host, unsigned how) OP1F_NAMED("kotekan_run");
extern "C" void kotekan_run(unsigned host, unsigned how) {
    if (host != (u32)SYNTH) return;
    u32 was = op1f_mask();
    kc_reset(&P);                                                     // a stop: the base ends what sounds, keys forgotten
    if (how == (u32)OP1F_RUN_STOP) runs_stop = runs_stop + 1u;
    else runs_on = runs_on + 1u;
    op1f_unmask(was);
}

extern "C" void kotekan_shown(unsigned where, unsigned how) OP1F_NAMED("kotekan_shown");
extern "C" void kotekan_shown(unsigned where, unsigned how) {
    shown_where = where;
    shown_how = how;
}

extern "C" void kotekan_changed(unsigned where, unsigned which, unsigned how) OP1F_NAMED("kotekan_changed");
extern "C" void kotekan_changed(unsigned where, unsigned which, unsigned how) {
    if (where != (unsigned)OP1F_KEEP_TAPE || which != 0 || how > 31u) return;
    told = told | (1u << how);
}

extern "C" int kotekan_input(const Op1fInput* e) OP1F_NAMED("kotekan_input");
extern "C" int kotekan_input(const Op1fInput* e) {
    if (e->bytes < (u32)INPUT_LEVEL_4 || !e->mine || e->what != (u32)OP1F_TURN || e->key >= (u32)KNOBS) return 0;
    turned[e->key] = turned[e->key] + (u32)(e->turn >> 10);           // 1024 a step; more than 0 up
    return 1;
}

// ---- the frame function's: texts, the settings, the keep, the page
static char* put(char* p, char* stop, const char* text) {
    while (*text && p < stop) *p++ = *text++;
    return p;
}

static char* put_number(char* p, char* stop, u32 v) {               // no division: powers of ten taken off
    static const u32 TENS[10] = { 1000000000u, 100000000u, 10000000u, 1000000u, 100000u, 10000u, 1000u, 100u, 10u, 1u };
    u32 started = 0;
    for (u32 i = 0; i < 10u; ++i) {
        u32 digit = 0;
        while (v >= TENS[i]) { v -= TENS[i]; ++digit; }
        if (digit || started || i == 9u) {
            if (p < stop) *p++ = (char)('0' + digit);
            started = 1;
        }
    }
    return p;
}

static int text_width(const char* text, int scale) {                 // as op1f_paint_text answers it
    int n = 0;
    while (text[n]) ++n;
    return n ? (6 * n - 1) * scale : 0;
}

static int same(const char* a, const char* b, u32 len) {
    for (u32 i = 0; i < len; ++i) if (!b[i] || a[i] != b[i]) return 0;
    return b[len] == 0;
}

// A whole number member of a flat JSON object, or -1 when it is missing or not a number of up to 6 digits.
static int member(const char* t, const char* want) {
    for (const char* p = t; *p; ++p) {
        if (*p != '"') continue;
        const char* name = p + 1;
        const char* q = name;
        while (*q && *q != '"') ++q;
        if (!*q) return -1;
        int hit = same(name, want, (u32)(q - name));
        p = q;
        if (!hit) continue;
        ++q;
        while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') ++q;
        if (*q != ':') return -1;
        ++q;
        while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') ++q;
        int n = 0, digits = 0;
        while (*q >= '0' && *q <= '9' && digits < 6) { n = n * 10 + (*q - '0'); ++q; ++digits; }
        return digits ? n : -1;
    }
    return -1;
}

static int rate_index(int steps) {
    for (u32 i = 0; i < (u32)KC_RATES; ++i) if ((int)KC_RATE_STEPS[i] == steps) return (int)i;
    return -1;
}

static struct {
    u32 started, stood, frames, said, turned_seen[KNOBS];
    u32 value[KNOBS];                                                 // style, map, rate (index), parts
    u32 tape, load_waits, held, held_word, keep_off;
    u32 said_counts, ticks_seen, quiet;
    char text[TEXT_ROOM];
} st;

static const u32 MOST[KNOBS] = { KC_STYLES - 1, KC_MAPS - 1, KC_RATES - 1, KC_PARTS - 1 };

static u32 word_of_values() { return kc_settings(st.value[0], st.value[1], st.value[2], st.value[3]); }

static void defaults() {
    st.value[0] = KC_DEFAULT_STYLE; st.value[1] = KC_DEFAULT_MAP; st.value[2] = KC_DEFAULT_RATE; st.value[3] = KC_DEFAULT_PARTS;
}

static void tape_loaded() {
    int got = op1f_value_get(CARD, OP1F_KEEP_TAPE, 0, KEPT, st.text, sizeof st.text);
    if (got == OP1F_NOT_NOW) return;                                  // asked again at the next frame, nothing said
    st.load_waits = 0;
    st.tape = 1;
    st.held = 0;
    int s = -1, m = -1, r = -1, p = -1, v = -1;
    if (got >= 0) {
        s = member(st.text, "style"); m = member(st.text, "map"); r = rate_index(member(st.text, "rate"));
        p = member(st.text, "parts"); v = member(st.text, "version");
    }
    if (v == VERSION && s >= 0 && s < KC_STYLES && m >= 0 && m < KC_MAPS && r >= 0 && p >= 0 && p < KC_PARTS) {
        st.value[0] = (u32)s; st.value[1] = (u32)m; st.value[2] = (u32)r; st.value[3] = (u32)p;
        st.held = 1;
        st.held_word = word_of_values();
    } else {
        defaults();                                                   // a tape with none of ours (new, or from before)
        if (got != OP1F_NO_FILE && st.said < (u32)SAID_MOST) {
            ++st.said;
            op1f_log(2, LOG_FILE, 2, "kotekan: the tape's %s is not this form (answer %d): the settings start as they come", KEPT, got);
        }
    }
}

static void tape_keep() {
    if (!st.tape || st.load_waits || st.keep_off) return;
    u32 w = word_of_values();
    if (st.held && st.held_word == w) return;
    char* stop = st.text + TEXT_ROOM - 1;
    char* p = put(st.text, stop, "{\"style\":");  p = put_number(p, stop, st.value[0]);
    p = put(p, stop, ",\"map\":");                 p = put_number(p, stop, st.value[1]);
    p = put(p, stop, ",\"rate\":");                p = put_number(p, stop, KC_RATE_STEPS[st.value[2]]);
    p = put(p, stop, ",\"parts\":");               p = put_number(p, stop, st.value[3]);
    p = put(p, stop, ",\"version\":1}");
    *p = 0;
    int answer = op1f_value_set(CARD, OP1F_KEEP_TAPE, 0, KEPT, st.text);
    if (answer == OP1F_NOT_NOW) return;                               // a load not yet told: set at the next frame
    if (answer != 0) {
        st.keep_off = 1;
        op1f_log(2, LOG_FILE, 3, "kotekan: the base does not keep %s with the tape (answer %d): the settings are the pack's alone", KEPT, answer);
        return;
    }
    st.held = 1;
    st.held_word = w;
}

static void paint_right(void* canvas, int x, int y, const char* text, unsigned colour, int scale) {
    op1f_paint_text(canvas, x - text_width(text, scale), y, text, colour, scale);
}

static int row_y(int lane_y, u32 word) {                             // a lane's row for a degree above the pokok
    int rel = (int)word - KC_SHOWN_ZERO;
    if (rel < -ZERO_ROW) rel = -ZERO_ROW;
    if (rel > ROWS - 1 - ZERO_ROW) rel = ROWS - 1 - ZERO_ROW;
    return lane_y + (ROWS - 1 - ZERO_ROW - rel) * ROW_H;
}

static void paint_lanes(void* canvas) {                              // two cycles: 8 steps, or 12 at 3 and 6 a beat
    u32 slots = P.shown_n == 12u ? 12u : 8u, cycle = slots == 12u ? 6u : 4u, now_i = P.shown_slot, on = P.running;
    int dx = slots == 12u ? LANE_DX12 : LANE_DX, dot = slots == 12u ? DOT12 : DOT;
    for (int lane = 0; lane < 2; ++lane) {
        int y0 = lane ? POLOS_Y : SANG_Y;
        op1f_paint_text(canvas, LEFT - 4, y0 + 2 * ROW_H, lane ? "P" : "S", GREY, 1);
        op1f_paint_box(canvas, LANE_X, row_y(y0, KC_SHOWN_ZERO) + 2, LANE_DX * 8 - 10, 1, on ? OCHRE : DARK_GREY);   // the pokok's line
        for (u32 i = 0; i < slots; ++i) {
            u32 w = P.shown[i];
            u32 d = lane ? (w & 255u) : (w >> 8 & 255u);
            int x = LANE_X + (int)i * dx;
            if (i == 0 || i == cycle) op1f_paint_box(canvas, x - 4, y0, 1, ROWS * ROW_H, DARK_GREY);   // the cycles' starts
            if (!d) continue;
            unsigned c = !on ? MID_GREY : i > now_i ? DARK_GREY : lane ? LIGHT_GREY : GREY;
            op1f_paint_box(canvas, x, row_y(y0, d), dot, ROW_H - 1, c);
        }
    }
    if (on && now_i < slots) op1f_paint_box(canvas, LANE_X + (int)now_i * dx, POLOS_Y + ROWS * ROW_H + 4, dot, 3, BLUE);   // the step now
}

static void paint_keys(void* canvas, u32 map) {
    static const int WHITE_PC[7] = { 0, 2, 4, 5, 7, 9, 11 };
    static const int BLACK_PC[5] = { 1, 3, 6, 8, 10 };
    static const int BLACK_AT[5] = { 0, 1, 3, 4, 5 };                 // after which white key
    int pc = -1;
    u32 pokok = P.shown_pokok;
    if (P.running && pokok) { pc = (int)pokok - 1; while (pc >= 12) pc -= 12; }
    for (int i = 0; i < 7; ++i) {
        int x = KEYS_X + i * WHITE_W;
        op1f_paint_box(canvas, x, KEYS_Y, WHITE_W - 2, WHITE_H, pc == WHITE_PC[i] ? OCHRE : LIGHT_GREY);
        for (int j = 0; j < 5; ++j) if (KC_MAP_PC[map][j] == WHITE_PC[i]) op1f_paint_box(canvas, x + 3, KEYS_Y + WHITE_H - 10, WHITE_W - 8, 6, BLUE);
    }
    for (int i = 0; i < 5; ++i) {
        int x = KEYS_X + (BLACK_AT[i] + 1) * WHITE_W - BLACK_W / 2 - 1;
        op1f_paint_box(canvas, x, KEYS_Y, BLACK_W, BLACK_H, pc == BLACK_PC[i] ? OCHRE : BLACK_KEY);
        for (int j = 0; j < 5; ++j) if (KC_MAP_PC[map][j] == BLACK_PC[i]) op1f_paint_box(canvas, x + 2, KEYS_Y + BLACK_H - 9, BLACK_W - 4, 5, BLUE);
    }
}

static void paint(void* canvas) {
    char* stop = st.text + TEXT_ROOM - 1;
    char* p;
    op1f_paint_text(canvas, LEFT, TOP, NAME, LIGHT_GREY, 2);
    u32 c = clock_from, t = tempo, whole = 0;
    while (t >= 100u && whole < 1000u) { t -= 100u; ++whole; }
    p = put(st.text, stop, c == (u32)OP1F_CLOCK_TAPE ? "tape " : c == (u32)OP1F_CLOCK_MIDI ? "midi clock " : "own clock ");
    p = put_number(p, stop, whole); p = put(p, stop, t < 10u ? ".0" : "."); p = put_number(p, stop, t); *p = 0;
    paint_right(canvas, RIGHT, TOP + 4, ticks ? st.text : "no clock yet", GREY, 1);
    static const char* const LABEL[KNOBS] = { "STYLE", "MAP", "RATE", "PARTS" };
    static const unsigned COLOUR[KNOBS] = { OP1F_COLOUR(0x3c, 0x60, 0x87), OP1F_COLOUR(0x8a, 0x65, 0x3c), OP1F_COLOUR(0xa2, 0xa4, 0xa6), OP1F_COLOUR(0xe5, 0x28, 0x17) };
    const char* value[KNOBS] = { STYLE_NAME[st.value[0]], MAP_NAME[st.value[1]], RATE_NAME[st.value[2]], PARTS_NAME[st.value[3]] };
    const char* sub[KNOBS] = { "", MAP_KEYS[st.value[1]], "", PARTS_SUB[st.value[3]] };
    for (int k = 0; k < KNOBS; ++k) {
        int x = LEFT + k * COL;
        op1f_paint_box(canvas, x, LABEL_Y - 6, 8, 3, COLOUR[k]);
        op1f_paint_text(canvas, x + 12, LABEL_Y - 8, LABEL[k], MID_GREY, 1);
        op1f_paint_text(canvas, x, VALUE_Y, value[k], COLOUR[k] == GREY ? LIGHT_GREY : COLOUR[k], 2);
        if (*sub[k]) op1f_paint_text(canvas, x, SUB_Y, sub[k], GREY, 1);
    }
    paint_lanes(canvas);
    paint_keys(canvas, st.value[1]);
    const char* hint = "hold a key: the pokok. polos below, sangsih above";
    unsigned hint_colour = MID_GREY;
    if (shown_how & (u32)OP1F_SHOWN_OFF) { hint = "switched off: the sequencer key starts it"; hint_colour = BLUE; }
    else if (st.quiet >= (u32)QUIET_FRAMES) { hint = "no ticks come: press play on the tape"; hint_colour = RED; }
    op1f_paint_text(canvas, LEFT, HINT_Y, hint, hint_colour, 1);
    if (st.tape && !st.keep_off) paint_right(canvas, RIGHT, HINT_Y, "kept with the tape", MID_GREY, 1);
}

// ---- the page's picture (kotekan-page.svg, analysis/screens/designs/kotekan/final/DESIGN.md): made once, its 77 parts
// found once, 64 of them hidden before the page shows; then each frame the page shows, every part set from the state,
// a call only where what it shows changed. Without the picture or any of its parts: paint() above, as before.
static const char PAGE_PICTURE[] = "kotekan-page.svg";
enum { N_STY = 0, N_MAP = 4, N_RATE = 8, N_PARTS = 13, N_MDOTS = 17, N_KLIT = 21, N_SAN = 31, N_POL = 43,
       N_STY_PIP = 55, N_MAP_PIP, N_RATE_PIP, N_CLOCK, N_GPOK, N_JUB, N_JUB_HIT, N_MALLET_POL, N_MALLET_SAN, N_PKL_ON,
       N_CYC8, N_CYC12, N_NOW, N_HINT_HOLD, N_HINT_TICKS, N_HINT_OFF, N_POKW, N_POKB, N_KEPT, N_PTX_SAN, N_PTX_POL,
       N_PTX_POK, NODES };
static const struct { const char* prefix; u32 count; } NODE_ROWS[8] = {
    { "kt_sty_", 4 }, { "kt_map_", 4 }, { "kt_rate_", 5 }, { "kt_parts_", 4 }, { "kt_mdots_", 4 }, { "kt_klit_", 10 },
    { "kt_san_", 12 }, { "kt_pol_", 12 } };
static const char* const NODE_ONE[NODES - N_STY_PIP] = {
    "kt_sty_pip", "kt_map_pip", "kt_rate_pip", "kt_clock", "kt_gpok", "kt_jub", "kt_jub_hit", "kt_mallet_pol",
    "kt_mallet_san", "kt_pkl_on", "kt_cyc8", "kt_cyc12", "kt_now", "kt_hint_hold", "kt_hint_ticks", "kt_hint_off",
    "kt_pokw", "kt_pokb", "kt_kept", "kt_ptx_san", "kt_ptx_pol", "kt_ptx_pok" };
static const char* const HINT_TEXT[3] = { "hold a key: the pokok", "no ticks: press play", "off: seq key starts it" };
// The unit's letters are as tall as their placeholder (15 px here: about 14 px a letter, not 10) and a text wider than
// the width it is set with loses its last letters with no mark (version 2 showed `hold a key: the pok`). So the
// clock's text ends at its placeholder, x 476, and has the room to PER BEAT (the widest, `tape 205.25`, is 148 px);
// the hints are centred on the lanes' middle, x 240 (the widest, `off: seq key starts it`, is 280 px of the lanes'
// 284) and may take 300. build.sh holds every text here to these rooms by the unit's own letter widths.
enum { CLOCK_ROOM = 156, HINT_ROOM = 300, MIDI_STILL = 5 };
enum { HINT_NONE = 3, ON = 1024, M_COS = 949, M_SIN = 384, STEP12 = 24 * 1024, PIP = 10 * 1024, KEY_DX = 9 * 1024 };
// Offline numbers in 1024ths (DESIGN.md "No division"): a step's x at 8 slots past its drawn place (24 px apart), a
// degree's y (-2.4 px a degree, -3..+6), the mallets' e and f per key [part 0 polos, 1 sangsih][key][hover, strike],
// the pokok's wedge per key (KX[k] - KX[0]), and the pokok key's x per pitch class (9 px a key; white, black).
static const int X8[8] = { 0, 14043, 28087, 42130, 56174, 70217, 84261, 98304 };
static const int DEG_Y[10] = { 7373, 4915, 2458, 0, -2458, -4915, -7373, -9830, -12288, -14746 };
static const int MALLET[2][10][2][2] = {
    { { { -53248, -1434 }, { -56688, 80507 } }, { { -26624, -717 }, { -30064, 81223 } }, { { 0, 0 }, { -3440, 81940 } },
      { { 26624, 717 }, { 23184, 82657 } }, { { 53248, 1434 }, { 49808, 83374 } }, { { 79872, 2150 }, { 76432, 84091 } },
      { { 106496, 2867 }, { 103056, 84807 } }, { { 133120, 3584 }, { 129680, 85524 } }, { { 159744, 4301 }, { 156304, 86241 } },
      { { 186368, 5018 }, { 182928, 86958 } } },
    { { { -186368, -5018 }, { -181009, 129353 } }, { { -159744, -4301 }, { -154385, 130070 } }, { { -133120, -3584 }, { -127761, 130786 } },
      { { -106496, -2867 }, { -101137, 131503 } }, { { -79872, -2150 }, { -74513, 132220 } }, { { -53248, -1434 }, { -47889, 132937 } },
      { { -26624, -717 }, { -21265, 133654 } }, { { 0, 0 }, { 5359, 134370 } }, { { 26624, 717 }, { 31983, 135087 } },
      { { 53248, 1434 }, { 58607, 135804 } } } };
static const int KEY_X[10] = { 0, 26624, 53248, 79872, 106496, 133120, 159744, 186368, 212992, 239616 };
static const int PC_KEY[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };   // white: its index; black: B(pc) - 1
static const u32 PC_BLACK = 1u << 1 | 1u << 3 | 1u << 6 | 1u << 8 | 1u << 10;

static struct {
    void* doc;                                                        // op1f_picture's answer, kept even when 0
    u32 asked, up;                                                    // up: the picture's page, every part found
    void* node[NODES];
    void* label[3];                                                   // the hints' texts as parts (op1f_text_node)
    unsigned char shown[NODES], turned[NODES];
    int e[NODES], f[NODES];
    u32 prev_slot, prev_steps, prev_pokok, key[2], strike[2], jub_hit;
    u32 tempo_said, from_said, refused;                               // the clock's tempo as shown; texts refused (logged once)
    char clock[24], hint_said[3][24];
} pg;

static void pg_show(u32 i, u32 on) {
    if (pg.shown[i] == (unsigned char)on) return;
    pg.shown[i] = (unsigned char)on;
    op1f_show(pg.node[i], (int)on);
}


static void pg_move(u32 i, int e, int f, u32 turned) {
    if (pg.e[i] == e && pg.f[i] == f && pg.turned[i] == (unsigned char)turned) return;
    pg.e[i] = e; pg.f[i] = f; pg.turned[i] = (unsigned char)turned;
    int m[9] = { ON, 0, 0, 0, ON, 0, e, f, ON };
    if (turned) { m[0] = M_COS; m[1] = -M_SIN; m[3] = M_SIN; m[4] = M_COS; }
    op1f_move(pg.node[i], m);
}

static void pg_one_of(u32 first, u32 count, u32 which) {
    for (u32 i = 0; i < count; ++i) if (i != which) pg_show(first + i, 0);
    pg_show(first + which, 1);
}

// Only when it changed. `which`: 0..2 the hints, 3 the clock; `width`: a wider text loses its last letters (the unit
// adds no mark), so no text here is wider; `align`: where the text stands on its placeholder, for good.
static void pg_text(u32 which, const char* part, char* said, u32 room, const char* text, int width, unsigned align) {
    u32 n = 0;
    while (text[n] && said[n] == text[n]) ++n;
    if (text[n] == 0 && said[n] == 0) return;
    if (!op1f_text_set(pg.doc, part, text, width, align)) {           // not taken: tried again later, said once
        if (!(pg.refused >> which & 1u)) {
            pg.refused |= 1u << which;
            op1f_log(2, LOG_FILE, 5, "kotekan: the base did not take the text of %s", part);
        }
        return;
    }
    n = 0;
    while (text[n] && n + 1u < room) { said[n] = text[n]; ++n; }
    said[n] = 0;
}

// A hint shown or hidden: its placeholder, and the text it became (a part of its own: op1f_text_node). Its text is
// set once, at the start; one the base did not take then is tried again each time the hint shows.
static void pg_hint(u32 h, u32 on) {
    u32 i = (u32)N_HINT_HOLD + h;
    if (pg.shown[i] == (unsigned char)on) return;
    pg.shown[i] = (unsigned char)on;
    op1f_show(pg.node[i], (int)on);
    if (on) {                                                         // no call when the text is in place
        pg_text(h, NODE_ONE[i - N_STY_PIP], pg.hint_said[h], sizeof pg.hint_said[h], HINT_TEXT[h], HINT_ROOM, OP1F_CENTRE);
        if (!pg.label[h]) pg.label[h] = op1f_text_node(pg.doc, NODE_ONE[i - N_STY_PIP]);
    }
    if (pg.label[h]) op1f_show(pg.label[h], (int)on);
}

// The tempo the clock shows: a MIDI clock's tempo wanders by a few hundredths from beat to beat, so it is shown again
// only when it moved by MIDI_STILL hundredths or more (or the clock changed); tape and own tempos as they are.
static u32 clock_tempo() {
    u32 c = clock_from, t = tempo;
    if (c == (u32)OP1F_CLOCK_MIDI && pg.from_said == c && t + (u32)MIDI_STILL > pg.tempo_said && t < pg.tempo_said + (u32)MIDI_STILL)
        return pg.tempo_said;
    pg.from_said = c; pg.tempo_said = t;
    return t;
}

static void clock_word(char* out, char* stop) {                       // tape 120.00, midi ..., own ...; no clock
    if (!ticks) { *put(out, stop, "no clock") = 0; return; }           // (`no clock yet` is 160 px: over the room)
    u32 c = clock_from, t = clock_tempo(), whole = 0;
    while (t >= 100u && whole < 1000u) { t -= 100u; ++whole; }
    char* p = put(out, stop, c == (u32)OP1F_CLOCK_TAPE ? "tape " : c == (u32)OP1F_CLOCK_MIDI ? "midi " : "own ");
    p = put_number(p, stop, whole); p = put(p, stop, t < 10u ? ".0" : "."); p = put_number(p, stop, t);
    *p = 0;
}

// Once, before the page is asked for: the picture made, its parts found, the start-hidden ones hidden, the hints'
// texts set (hidden). Answers the document the page stands in with: the picture's, or 0 for the painted page.
static void* page_make() {
    pg.asked = 1;
    pg.doc = op1f_picture(CARD, PAGE_PICTURE);
    if (!pg.doc) {
        op1f_log(2, LOG_FILE, 5, "kotekan: no picture %s: its page is painted", PAGE_PICTURE);
        return 0;
    }
    char name[24];
    u32 i = 0, missing = 0;
    const char* first_missing = 0;
    for (u32 r = 0; r < 8u; ++r)
        for (u32 k = 0; k < NODE_ROWS[r].count; ++k, ++i) {
            char* p = put_number(put(name, name + 20, NODE_ROWS[r].prefix), name + 23, k);
            *p = 0;
            pg.node[i] = op1f_node(pg.doc, name);
            if (!pg.node[i]) ++missing;
        }
    for (; i < (u32)NODES; ++i) {
        pg.node[i] = op1f_node(pg.doc, NODE_ONE[i - N_STY_PIP]);
        if (!pg.node[i]) { ++missing; if (!first_missing) first_missing = NODE_ONE[i - N_STY_PIP]; }
    }
    if (missing) {
        op1f_log(2, LOG_FILE, 5, "kotekan: the picture %s lacks %u part(s)%s%s: its page is painted", PAGE_PICTURE, missing,
                 first_missing ? ", such as " : "", first_missing ? first_missing : "");
        return 0;
    }
    for (i = 0; i < (u32)NODES; ++i) pg.shown[i] = 1;                 // drawn visible, at rest
    for (i = 0; i < 4u; ++i) if (i != 2u) pg_show(N_STY + i, 0);
    for (i = 1; i < 4u; ++i) { pg_show(N_MAP + i, 0); pg_show(N_PARTS + i, 0); pg_show(N_MDOTS + i, 0); }
    for (i = 0; i < 5u; ++i) if (i != 2u) pg_show(N_RATE + i, 0);
    for (i = 0; i < 10u; ++i) pg_show(N_KLIT + i, 0);
    for (i = 0; i < 12u; ++i) { pg_show(N_SAN + i, 0); pg_show(N_POL + i, 0); }
    static const unsigned char HIDDEN[14] = { N_GPOK, N_JUB, N_JUB_HIT, N_PKL_ON, N_CYC8, N_CYC12, N_NOW, N_HINT_HOLD,
                                              N_HINT_TICKS, N_HINT_OFF, N_POKW, N_POKB, N_KEPT, N_PTX_POK };
    for (i = 0; i < 14u; ++i) pg_show(HIDDEN[i], 0);
    for (u32 h = 0; h < 3u; ++h) {                                    // set once, hidden with their placeholders
        pg.hint_said[h][0] = 1;                                       // nothing written yet
        pg_text(h, NODE_ONE[N_HINT_HOLD + h - N_STY_PIP], pg.hint_said[h], sizeof pg.hint_said[h], HINT_TEXT[h], HINT_ROOM, OP1F_CENTRE);
        pg.label[h] = op1f_text_node(pg.doc, NODE_ONE[N_HINT_HOLD + h - N_STY_PIP]);
        if (pg.label[h]) op1f_show(pg.label[h], 0);
    }
    pg.clock[0] = 1;                                                  // nothing written yet: the first word is written
    pg.from_said = ~0u;
    char word[sizeof pg.clock];
    clock_word(word, word + sizeof word - 1);
    pg_text(3, "kt_clock", pg.clock, sizeof pg.clock, word, CLOCK_ROOM, OP1F_RIGHT);   // the placeholder's stroke never shows
    pg.key[0] = 2; pg.key[1] = 7;                                     // the mallets hover where they are drawn
    pg.prev_slot = P.shown_slot; pg.prev_steps = P.steps; pg.prev_pokok = P.shown_pokok;
    pg.up = 1;
    op1f_log(3, LOG_FILE, 5, "kotekan: its picture %s made, %u parts found", PAGE_PICTURE, (u32)NODES);
    return pg.doc;
}

static u32 key_of(u32 degree, int rel) {                              // the gangsa's key, folded by octaves (5 keys)
    while (degree >= 10u) degree -= 10u;
    int k = (int)degree + rel;
    while (k < 0) k += 5;
    while (k > 9) k -= 5;
    return (u32)k;
}

static void page_frame() {
    u32 v0 = st.value[0], v1 = st.value[1], v2 = st.value[2], v3 = st.value[3];
    u32 pol_on = v3 != (u32)KC_SANGSIH_ONLY, san_on = v3 != (u32)KC_POLOS_ONLY, pokok_on = v3 == (u32)KC_BOTH_POKOK;
    u32 running = P.running, holding = P.holding, live = running || holding;
    u32 n = P.shown_n == 12u ? 12u : 8u, slot = P.shown_slot, steps = P.steps, pokok = P.shown_pokok;
    u32 degree = P.shown_degree;
    u32 hint = (shown_how & (u32)OP1F_SHOWN_OFF) ? 2u : st.quiet >= (u32)QUIET_FRAMES ? 1u : (!pokok && !holding) ? 0u : (u32)HINT_NONE;
    // the four values: words, pips, map dots, PARTS' icon, the jublag
    pg_one_of(N_STY, 4, v0); pg_one_of(N_MAP, 4, v1); pg_one_of(N_RATE, 5, v2); pg_one_of(N_PARTS, 4, v3);
    pg_one_of(N_MDOTS, 4, v1);
    pg_move(N_STY_PIP, (int)v0 * PIP, 0, 0); pg_move(N_MAP_PIP, (int)v1 * PIP, 0, 0); pg_move(N_RATE_PIP, (int)v2 * PIP, 0, 0);
    pg_show(N_PTX_SAN, san_on); pg_show(N_PTX_POL, pol_on); pg_show(N_PTX_POK, pokok_on); pg_show(N_JUB, pokok_on);
    // a step: the parts that strike it, the jublag at a cycle's start or a new pokok
    u32 stepped = steps != pg.prev_steps || slot != pg.prev_slot;
    if (stepped && running && hint == (u32)HINT_NONE && slot < n) {
        u32 w = P.shown[slot];
        u32 b[2] = { w & 255u, w >> 8 & 255u };
        u32 on[2] = { pol_on, san_on };
        for (u32 j = 0; j < 2u; ++j)
            if (on[j] && b[j]) { pg.key[j] = key_of(degree, (int)b[j] - KC_SHOWN_ZERO); pg.strike[j] = 2; }
    }
    if (pokok_on && pokok && ((pokok != pg.prev_pokok) || (stepped && running && (slot == 0u || slot == (n == 12u ? 6u : 4u)))))
        pg.jub_hit = 2;
    pg.prev_slot = slot; pg.prev_steps = steps; pg.prev_pokok = pokok;
    pg_show(N_JUB_HIT, pokok_on && pg.jub_hit);
    u32 lit = 0;
    for (u32 j = 0; j < 2u; ++j) {
        u32 on = j ? san_on : pol_on, k = pg.key[j], hit = on && pg.strike[j] != 0u;
        if (hit) lit |= 1u << k;
        pg_move(N_MALLET_POL + j, MALLET[j][k][hit][0], MALLET[j][k][hit][1], hit);
        pg_show(N_MALLET_POL + j, on);
    }
    for (u32 k = 0; k < 10u; ++k) pg_show(N_KLIT + k, lit >> k & 1u);
    for (u32 j = 0; j < 2u; ++j) if (pg.strike[j]) --pg.strike[j];
    if (pg.jub_hit) --pg.jub_hit;
    // the lanes: 24 steps, the step now, the cycles' marks, the pokok lines
    for (u32 i = 0; i < 12u; ++i) {
        u32 w = i < n ? P.shown[i] : 0u;
        for (u32 j = 0; j < 2u; ++j) {
            u32 at = (j ? N_SAN : N_POL) + i, bb = j ? w >> 8 & 255u : w & 255u;
            if (!bb || !(j ? san_on : pol_on) || hint != (u32)HINT_NONE) { pg_show(at, 0); continue; }
            int rel = (int)bb - KC_SHOWN_ZERO;
            if (rel < -3) rel = -3;
            if (rel > 6) rel = 6;
            pg_move(at, n == 12u ? 0 : X8[i], DEG_Y[rel + 3], 0);
            pg_show(at, 1);
        }
    }
    if (running && hint == (u32)HINT_NONE && slot < n) {
        pg_move(N_NOW, (int)slot * STEP12 + (n == 12u ? 0 : X8[slot]), 0, 0);
        pg_show(N_NOW, 1);
    } else pg_show(N_NOW, 0);
    pg_show(N_CYC8, hint == (u32)HINT_NONE && n == 8u);
    pg_show(N_CYC12, hint == (u32)HINT_NONE && n == 12u);
    pg_show(N_PKL_ON, live);
    // the pokok: its wedge on the gangsa's rail, its key on the map's keyboard
    if (live && pokok) {
        u32 d = degree, pc = pokok - 1u;
        while (d >= 10u) d -= 10u;
        while (pc >= 12u) pc -= 12u;
        pg_move(N_GPOK, KEY_X[d], 0, 0);
        pg_show(N_GPOK, 1);
        u32 black = PC_BLACK >> pc & 1u, at = black ? N_POKB : N_POKW;
        pg_move(at, PC_KEY[pc] * KEY_DX, 0, 0);
        pg_show(at, 1);
        pg_show(black ? N_POKW : N_POKB, 0);
    } else { pg_show(N_GPOK, 0); pg_show(N_POKW, 0); pg_show(N_POKB, 0); }
    // the hints, the clock, the keep
    for (u32 h = 0; h < 3u; ++h) pg_hint(h, h == hint);
    char word[sizeof pg.clock];
    clock_word(word, word + sizeof word - 1);
    pg_text(3, "kt_clock", pg.clock, sizeof pg.clock, word, CLOCK_ROOM, OP1F_RIGHT);
    pg_show(N_KEPT, st.tape && !st.keep_off);
}

extern "C" void kotekan_frame(const Op1fFrame* now) OP1F_NAMED("kotekan_frame");
extern "C" void kotekan_frame(const Op1fFrame* now) {
    if (now->bytes < (u32)FRAME_LEVEL_4) return;
    if (!st.started) {
        st.started = 1;
        defaults();
        for (u32 k = 0; k < (u32)KNOBS; ++k) st.turned_seen[k] = turned[k];
        live = word_of_values();
    }
    if (!st.stood) {                                                  // once: the picture's page, or the blank page painted below
        void* doc = pg.asked ? (pg.up ? pg.doc : 0) : page_make();
        st.stood = op1f_page_stand_in(CARD, ASK, doc) ? 1u : 0u;
        op1f_log(3, LOG_FILE, 1, st.stood ? "kotekan: its page stands in for the sequencer's screen" : "kotekan: the base did not take its page");
        if (!st.stood) {                                              // asked once only: the blank page, painted as before
            st.stood = 2;
            if (pg.up) { pg.up = 0; op1f_log(2, LOG_FILE, 5, "kotekan: its picture %s is not shown: its page is painted", PAGE_PICTURE); }
        }
    }
    u32 t = told;                                                     // changed runs on this thread, never inside this call
    told = 0;
    if (t & ((1u << OP1F_KEPT_LOADED) | (1u << OP1F_KEPT_MADE))) st.load_waits = 1;
    if (st.load_waits && !st.keep_off) tape_loaded();
    for (u32 k = 0; k < (u32)KNOBS; ++k) {
        u32 now_turned = turned[k];
        int steps = (int)(now_turned - st.turned_seen[k]);
        st.turned_seen[k] = now_turned;
        if (!steps) continue;
        int v = (int)st.value[k] + steps;
        st.value[k] = v < 0 ? 0u : v > (int)MOST[k] ? MOST[k] : (u32)v;
    }
    live = word_of_values();
    tape_keep();
    u32 seen = ticks;                                                 // a key held and no tick for half a second
    if (seen != st.ticks_seen || !P.holding) { st.ticks_seen = seen; st.quiet = 0; }
    else if (st.quiet < (u32)QUIET_FRAMES) ++st.quiet;
    if (pg.up) page_frame();                                          // every frame, shown or not: no stale first frame
    else if (now->page == CARD && now->canvas) paint(now->canvas);
    if (++st.frames >= (u32)PERIOD_FRAMES) {
        st.frames = 0;
        u32 sum = keys_taken + notes_on + notes_off + refused + late + runs_on + runs_stop;
        if (sum != st.said_counts && st.said < (u32)SAID_MOST) {
            st.said_counts = sum;
            ++st.said;
            op1f_log(3, LOG_FILE, 4, "kotekan: %u key(s), notes %u on %u off, %u refused, %u late, %u silence(s); runs %u on %u stop; %u ticks; %u steps, %u figures, %u skipped; settings %x",
                     keys_taken, notes_on, notes_off, refused, late, silences, runs_on, runs_stop, ticks, P.steps, P.starts, P.skipped, live);
        }
    }
}
