// gangsa: a synth engine of level 9 (docs/level-9.md of open-field 0.12, base.pack 30): struck metal and wood, each
// voice a bank of decaying sine partials (core/gangsa_core.h), stereo, with the ombak twin of a gamelan pair. Names no
// address of the firmware and changes no place of it. Compiled for the PC as it is, with OP1F_HOST (host/pack_sim).

#include "build_r6e_card.h"
#include "gangsa_core.h"

typedef unsigned int u32;

extern "C" const Op1fCard op1f_card_gangsa OP1F_NAMED("op1f_card_gangsa");

enum { VALUES = 8, ASK = 0, FRAME_LEVEL_4 = 40, BLOCK_LEVEL_9 = 52, VOICE_BYTES = 104 };
enum { MATERIAL = 0, MALLET = 1, DECAY = 2, OMBAK = 3, DAMP = 4, STRIKE = 5, CLICK = 6, WIDTH = 7 };
static_assert((int)GS_BEGINS == (int)OP1F_VOICE_BEGINS && (int)GS_ENDS == (int)OP1F_VOICE_ENDS && (int)GS_FRESH == (int)OP1F_VOICE_FRESH,
              "the core's events are the base's");
static_assert(sizeof(GsVoice) == (unsigned)VOICE_BYTES, "voice_bytes in recipe.json is sizeof(GsVoice)");
static_assert(sizeof(GsValues) == 4u * VALUES, "the values in the recipe's order are GsValues' members");

// Version 6 (2026-11-24, FIX-NOTES-2026-11-24.md): a voice the unit has not rendered for REST_FRAMES frames was silent
// to the ear (the unit's envelope had ended it), whatever its bars still held; a note that begins on it starts from
// rest, as after a hand-over. Before, the strike was added to what the bars held when the unit stopped rendering,
// however long ago, and the same key struck again and again grew brighter. The frame function counts frames in one
// word; render keeps, for each of the six voices, the count at its last block. While the display thread is held up
// the count stands still and nothing counts as rested: the strike is then added as before.
enum { VOICES = 6, REST_FRAMES = 3 };
static volatile u32 frames_counted;                   // the frame function's alone to write
static u32 voice_seen[VOICES];                        // render's alone: frames_counted at each voice's last block

// Audio thread: no call out, no division, no float; all 256 words of out written.
extern "C" void gangsa_render(const Op1fVoiceBlock* now) OP1F_NAMED("gangsa_render");
extern "C" GS_RENDER_OPT void gangsa_render(const Op1fVoiceBlock* now) {
    int* out = now->out;
    GsVoice* v = (GsVoice*)now->state;
    if (now->bytes < (u32)BLOCK_LEVEL_9 || v == 0) {
        for (u32 k = 0; k < 2u * (u32)GS_FRAMES; ++k) out[k] = 0;
        return;
    }
    u32 events = now->events, at = frames_counted;
    if (now->voice < (u32)VOICES) {
        if ((events & (u32)OP1F_VOICE_BEGINS) && at - voice_seen[now->voice] >= (u32)REST_FRAMES) events |= (u32)OP1F_VOICE_FRESH;
        voice_seen[now->voice] = at;
    }
    gs_render(v, (const GsValues*)now->values, now->step, (u32)now->velocity, events, out);
}
// No `reset` since version 6: gs_render clears the voice (104 bytes) at OP1F_VOICE_FRESH, in the voice's next block.
// With a reset the base holds each voice handed over silent until a later frame has called it (17 ms or more).

// ---------------------------------------------------------------- the page
// Values as set (level 8's op1f_param_get; as heard would need level 10). It reads the material tables, never a voice.

static const unsigned COLOURS[4] = {OP1F_COLOUR(0x3c, 0x60, 0x87), OP1F_COLOUR(0x8a, 0x65, 0x3c), OP1F_COLOUR(0xa2, 0xa4, 0xa6), OP1F_COLOUR(0xe5, 0x28, 0x17)};
static const unsigned NAME_GREY = OP1F_COLOUR(0xa2, 0xa4, 0xa6);
static const unsigned DARK_GREY = OP1F_COLOUR(0x37, 0x3d, 0x42);
static const unsigned MAIN_LINE = OP1F_COLOUR(0xcd, 0xd0, 0xc2);   // lightgrey of the palette
static const int MIDDLES[4] = {60, 180, 300, 420};
static const char NAMES[VALUES][9] = {"material", "mallet", "decay", "ombak", "damp", "strike", "click", "width"};
enum { TOP_Y = 18, TOP_SCALE = 3, MAT_Y = 22, MAT_SCALE = 2, TOP_NAME_Y = 46, RULE1_Y = 62, LOW_Y = 72, LOW_SCALE = 2,
       LOW_NAME_Y = 90, RULE2_Y = 104, RULE_X = 16, RULE_WIDE = 448, BASE_Y = 214, TALL = 72, LEFT = 48, BAR = 3, TWIN_DX = 5,
       CAPTION_Y = 112, OFF_Y = 128 };

static volatile u32 shown_where, shown_how;           // single words, written by shown, read by the frame function
static struct { u32 stood, refusal_said; } page;

extern "C" void gangsa_shown(unsigned where, unsigned how) OP1F_NAMED("gangsa_shown");
extern "C" void gangsa_shown(unsigned where, unsigned how) {
    shown_where = where;
    shown_how = how;
}

// The notes heard (level 5, ask 1): any thread, the audio thread among them, so an input function's rules: two single
// words written, nothing called, nothing waited for. The frame function swings the mallet when the counter moves.
enum { NOTE_BYTES = 24, HOST_SYNTH = 0, MODE_SYNTH = 0 };
static volatile u32 heard_velocity, heard_strikes;
extern "C" void gangsa_note(const Op1fNote* event) OP1F_NAMED("gangsa_note");
extern "C" void gangsa_note(const Op1fNote* event) {
    if (event->bytes < (u32)NOTE_BYTES || event->what != (u32)OP1F_NOTE_BEGINS || event->host != (u32)HOST_SYNTH) return;
    heard_velocity = event->velocity;
    heard_strikes = heard_strikes + 1u;
}

static u32 put_number(char* out, u32 u) {             // by subtraction: nothing is divided
    char digits[12];
    u32 d = 0, at = 0;
    do {
        u32 tens = 0;
        while (u >= 10u) { u -= 10u; ++tens; }
        digits[d++] = (char)('0' + u);
        u = tens;
    } while (u && d < 11u);
    while (d) out[at++] = digits[--d];
    out[at] = 0;
    return at;
}

static u32 put_text(char* out, const char* t) {
    u32 at = 0;
    while (t[at]) { out[at] = t[at]; ++at; }
    out[at] = 0;
    return at;
}

static u32 put_hundredths(char* out, u32 c) {         // c / 100 with two decimals
    u32 whole = 0;
    while (c >= 100u) { c -= 100u; ++whole; }
    u32 at = put_number(out, whole);
    out[at++] = '.';
    u32 tens = 0;
    while (c >= 10u) { c -= 10u; ++tens; }
    out[at++] = (char)('0' + tens);
    out[at++] = (char)('0' + c);
    out[at] = 0;
    return at;
}

static void centred(void* canvas, int middle, int y, const char* text, unsigned colour, int scale) {
    int n = 0;
    while (text[n]) ++n;
    op1f_paint_text(canvas, middle - (((6 * n - 1) * scale) >> 1), y, text, colour, scale);
}

// log2 of a weight (Q15, > 0), Q4: whole octaves by shifting, the rest linear
static int log2_q4(int w) {
    if (w <= 0) return -15 * 16;
    int n = 0;
    while (w < 16384) { w <<= 1; ++n; }
    return ((w - 16384) >> 10) - (n << 4);
}

// a partial's height: what a strike at the set mallet and strike gives it (velocity 100), on a log scale: 72 px for
// the loudest, 12 px less a 6 dB under it, at least 2 px. Answers the count; h[k] the heights (shared by both pages).
static int partial_heights(int material, int mallet, int place, int* h) {
    int n = gs_partial_count(material), l[GS_PARTIALS], top = -15 * 16;
    for (int k = 0; k < n; ++k) {
        l[k] = log2_q4(gs_partial_struck(material, k, mallet, place, 100));
        if (l[k] > top) top = l[k];
    }
    for (int k = 0; k < n; ++k) {
        h[k] = TALL - (((top - l[k]) * 12) >> 4);
        if (h[k] < 2) h[k] = 2;
    }
    return n;
}

// a line a partial: x by log2 of its ratio (3/16 px a Q8 step: 8 octaves from 1/2 in 384 px), tall by partial_heights
static void partials(void* canvas, int material, int mallet, int place, int ombak) {
    int h[GS_PARTIALS];
    int n = partial_heights(material, mallet, place, h);
    for (int k = 0; k < n; ++k) {
        int x = LEFT + (((gs_partial_octaves(material, k) + 256) * 3) >> 4);
        op1f_paint_box(canvas, x, BASE_Y - h[k], BAR, h[k], MAIN_LINE);
        if (ombak > 0 && k < GS_TWINS) op1f_paint_box(canvas, x + TWIN_DX, BASE_Y - h[k], BAR, h[k], COLOURS[3]);
    }
    op1f_paint_box(canvas, RULE_X, BASE_Y, RULE_WIDE, 1, DARK_GREY);
}

static void paint(void* canvas, u32 where, u32 how) {
    char text[24];
    int value[VALUES], got = 0;
    for (u32 i = 0; i < (u32)VALUES; ++i) {
        value[i] = 0;
        int ok = op1f_param_get(&op1f_card_gangsa, ASK, where, i, &value[i]) == 0;
        got += ok;
        u32 top = i < 4u;
        int middle = MIDDLES[i & 3u];
        if (!ok) put_text(text, "-");
        else if (i == (u32)MATERIAL) put_text(text, gs_material_name(value[i]));
        else put_number(text, value[i] < 0 ? 0u : (u32)value[i]);
        if (i == (u32)MATERIAL && ok) centred(canvas, middle, MAT_Y, text, COLOURS[0], MAT_SCALE);
        else centred(canvas, middle, top ? TOP_Y : LOW_Y, text, i == (u32)WIDTH && value[OMBAK] <= 0 ? DARK_GREY : COLOURS[i & 3u],
                     top ? TOP_SCALE : LOW_SCALE);              // width is heard only with ombak on
        centred(canvas, middle, top ? TOP_NAME_Y : LOW_NAME_Y, NAMES[i], NAME_GREY, 1);
    }
    op1f_paint_box(canvas, RULE_X, RULE1_Y, RULE_WIDE, 1, DARK_GREY);
    op1f_paint_box(canvas, RULE_X, RULE2_Y, RULE_WIDE, 1, DARK_GREY);
    if (got == VALUES) {
        u32 at = put_text(text, "ring ");                  // held, then after key up (damp); ms / 10, near enough
        at += put_hundredths(text + at, ((u32)gs_decay_ms(value[DECAY]) * 6554u) >> 16);
        at += put_text(text + at, " / ");
        at += put_hundredths(text + at, ((u32)gs_damped_ms(value[DECAY], value[DAMP]) * 6554u) >> 16);
        put_text(text + at, " s");
        op1f_paint_text(canvas, RULE_X, CAPTION_Y, text, NAME_GREY, 1);
        if (value[OMBAK] > 0) {
            at = put_text(text, "beat ");
            at += put_hundredths(text + at, (u32)gs_ombak_centihz(value[OMBAK]));
            put_text(text + at, " hz");
            op1f_paint_text(canvas, 300, CAPTION_Y, text, COLOURS[3], 1);
        }
        partials(canvas, value[MATERIAL], value[MALLET], value[STRIKE], value[OMBAK]);
    }
    if (how & (u32)OP1F_SHOWN_OFF) centred(canvas, 240, OFF_Y, "switched off", COLOURS[3], 1);
}

// ---------------------------------------------------------------- the page in a picture of its own
// gangsa-page.svg (gangsa-page.svg.make, analysis/screens/designs/gangsa/final/DESIGN.md): made once, its parts found
// once and the start-hidden ones hidden before it stands in. A part is shown, hidden or moved, and a text set, only when
// what drives it changed. Where the picture or a part is missing, the page above is painted on the blank page instead.
// op1f_move's nine numbers are 1024ths: x' = (a x + c y + e) / 1024, y' = (b x + d y + f) / 1024; a move replaces the
// part's drawn transform.

static const char PAGE_PICTURE[] = "gangsa-page.svg";
enum { DIAL = 8, METER = 10, DIGIT_SET = 22, D_H1 = 0, D_T0 = 1, D_U0 = 11, D_DASH = 21, BLADES = GS_PARTIALS, TWIN_BLADES = 4,
       SAID = 9, SAID_BYTES = 16, UNREAD = -1, NOT_YET = -2, BLADE_Y = 212, BLADE_X = 108, SPOT_STEP = -121, SWAY = 2048 };
static_assert((int)TWIN_BLADES == (int)GS_TWINS, "a red blade a twin partial");
// say() keeps SAID_BYTES - 1 characters: a longer text would never compare equal and be set every frame.
// The longest today is 12 ("switched off"; ring "19.9/19.9 s" is 11, v3 "10.00 hz" 8): keep every text under 16.
static_assert((int)SAID_BYTES >= 13, "the longest text, switched off, and its end");
// the needle at material i: turned i x 150/7 deg about the dial's pivot (43.3, 67.6) (DESIGN.md tables.txt)
static const int NEEDLE[DIAL][9] = {
    {1024, 0, 0, 0, 1024, 0, 0, 0, 1024}, {953, 374, 0, -374, 953, 0, 28355, -11414, 1024},
    {751, 696, 0, -696, 751, 0, 58919, -11680, 1024}, {444, 923, 0, -923, 444, 0, 87468, -760, 1024},
    {77, 1021, 0, -1021, 77, 0, 110055, 19834, 1024}, {-302, 979, 0, -979, -302, 0, 123555, 47257, 1024},
    {-638, 801, 0, -801, -638, 0, 126104, 77716, 1024}, {-887, 512, 0, -512, -887, 0, 117349, 107001, 1024}};
static const unsigned char METER_AT[METER] = {1, 13, 26, 39, 51, 64, 77, 89, 102, 115};   // a bar lit from this ombak on
// the mallet swung about its grip at a strike, by mallet >> 5: -4, -6, -8, -10 deg (DESIGN.md tables.txt); f += the spot's
enum { SWINGS = 4, SWING_FRAMES = 4, REST = -1 };
static const int SWING[SWINGS][9] = {
    {1022, -71, 0, 71, 1022, 0, -1962, 17237, 1024}, {1018, -107, 0, 107, 1018, 0, -2490, 25896, 1024},
    {1014, -143, 0, 143, 1014, 0, -2716, 34569, 1024}, {1008, -178, 0, 178, 1008, 0, -2639, 43244, 1024}};
// the placeholders, in the order of pg.said; their alignment is the one of their first use
enum { S_V0, S_V3, S_V4, S_V5, S_V6, S_V7, S_V7_DIM, S_RING, S_STATE };
static const char* const SAID_PART[SAID] = {"gangsa_v0", "gangsa_v3", "gangsa_v4", "gangsa_v5", "gangsa_v6", "gangsa_v7", "gangsa_v7_dim", "gangsa_ring", "gangsa_state"};
static const unsigned char SAID_ALIGN[SAID] = {OP1F_LEFT, OP1F_RIGHT, OP1F_LEFT, OP1F_LEFT, OP1F_RIGHT, OP1F_LEFT, OP1F_LEFT, OP1F_RIGHT, OP1F_CENTRE};

static struct {
    void* doc;                                        // the picture standing in, or 0: the blank page, painted
    u32 asked, missing_said;
    void* needle; void* md[DIAL]; void* mal[DIGIT_SET]; void* dec[DIGIT_SET]; void* om[METER];
    void* wlab; void* wlab_dim; void* art; void* twin; void* spot; void* mallet; void* hit; void* p[BLADES]; void* t[TWIN_BLADES];
    int last[VALUES], off, lit, needle_on, mal_at[3], dec_at[3], lit_bars, dim, twin_on, sign, p_on[BLADES], p_s[BLADES], p_e[BLADES], blades_n, t_n;
    u32 half, sway_at, owed;                         // owed: a text the base did not take, set again next frame
    u32 strikes_at, strikes_seen, strikes_said, up;  // heard_strikes as last seen; notes heard while the page swung (a
                                                     // chord's notes each), as last logged; the page up at the last frame
    int swing_left, mal_pose, mal_strike, hit_on;    // frames of swing left; the mallet as moved (REST or SWING row), shown
    char said[SAID][SAID_BYTES];
} pg;

static char* put_index(char* out, const char* a, int i) { // a + i, i < 100
    u32 at = put_text(out, a);
    put_number(out + at, (u32)i);
    return out;
}

static void* find(void* d, const char* name, u32* missing) {
    void* p = op1f_node(d, name);
    if (p == 0 && (*missing)++ == 0u) op1f_log(2, "../src/main/gangsa.cpp", 2, "gangsa: its picture has no part %s: the page is painted instead", name);
    return p;
}

static void find_digits(void* d, const char* slot, void** set, u32* missing) {   // slot: "gangsa_mal_" or "gangsa_dec_"
    char name[24], a[16];
    u32 at = put_text(a, slot);
    put_text(a + at, "h1"); set[D_H1] = find(d, a, missing);
    for (int k = 0; k < 10; ++k) {
        put_text(a + at, "t"); set[D_T0 + k] = find(d, put_index(name, a, k), missing);
        put_text(a + at, "u"); set[D_U0 + k] = find(d, put_index(name, a, k), missing);
    }
    put_text(a + at, "dash"); set[D_DASH] = find(d, a, missing);
}

// The picture, made once; its parts, found once; the start-hidden ones hidden before the page stands in.
static void make_page() {
    void* d = op1f_picture(&op1f_card_gangsa, PAGE_PICTURE);
    if (d == 0) {
        op1f_log(2, "../src/main/gangsa.cpp", 3, "gangsa: its picture %s was not made: the page is painted instead", PAGE_PICTURE);
        return;
    }
    char name[24];
    u32 missing = 0;
    pg.needle = find(d, "gangsa_needle", &missing);
    for (int k = 0; k < DIAL; ++k) pg.md[k] = find(d, put_index(name, "gangsa_md", k), &missing);
    find_digits(d, "gangsa_mal_", pg.mal, &missing);
    find_digits(d, "gangsa_dec_", pg.dec, &missing);
    for (int k = 0; k < METER; ++k) pg.om[k] = find(d, put_index(name, "gangsa_om", k), &missing);
    pg.wlab = find(d, "gangsa_wlab", &missing);
    pg.wlab_dim = find(d, "gangsa_wlab_dim", &missing);
    pg.art = find(d, "gangsa_art", &missing);
    pg.twin = find(d, "gangsa_twin", &missing);
    pg.spot = find(d, "gangsa_spot", &missing);
    pg.mallet = find(d, "gangsa_mallet", &missing);
    pg.hit = find(d, "gangsa_hit", &missing);
    for (int k = 0; k < BLADES; ++k) pg.p[k] = find(d, put_index(name, "gangsa_p", k), &missing);
    for (int k = 0; k < TWIN_BLADES; ++k) pg.t[k] = find(d, put_index(name, "gangsa_t", k), &missing);
    for (int k = 0; k < SAID; ++k) find(d, SAID_PART[k], &missing);   // the placeholders are there too
    if (missing) return;                    // the made document stays unused (asked once only): one of the card's 8, harmless
    // start hidden (DESIGN.md): the 8 dots, both digit sets, the meter, the twin, its blades, the hit, WIDTH dimmed
    for (int k = 0; k < DIAL; ++k) op1f_show(pg.md[k], 0);
    for (int k = 0; k < DIGIT_SET; ++k) { op1f_show(pg.mal[k], 0); op1f_show(pg.dec[k], 0); }
    for (int k = 0; k < METER; ++k) op1f_show(pg.om[k], 0);
    op1f_show(pg.twin, 0);
    for (int k = 0; k < TWIN_BLADES; ++k) op1f_show(pg.t[k], 0);
    op1f_show(pg.hit, 0);
    op1f_show(pg.wlab_dim, 0);
    for (int k = 0; k < VALUES; ++k) pg.last[k] = NOT_YET;
    pg.lit = -1;                                                    // as drawn and hidden above:
    pg.needle_on = 1; pg.off = pg.lit_bars = pg.dim = pg.twin_on = 0;  // needle and art shown, no bar, WIDTH red, no twin
    for (int k = 0; k < 3; ++k) pg.mal_at[k] = pg.dec_at[k] = -1;
    for (int k = 0; k < BLADES; ++k) { pg.p_on[k] = 1; pg.p_s[k] = pg.p_e[k] = -1; }   // drawn shown; not moved yet
    pg.blades_n = -1;
    pg.t_n = 0;                                                     // hidden above
    pg.strikes_at = heard_strikes; pg.swing_left = 0;               // strikes before the page are not swung
    pg.mal_pose = REST; pg.mal_strike = NOT_YET; pg.hit_on = 0;     // the mallet moved at the first frame; the hit hidden
    for (int k = 0; k < SAID; ++k) pg.said[k][0] = 1;               // nothing written yet: the first text, even "", is
    pg.doc = d;
}

static void say(int i, const char* text) {
    u32 n = 0;
    while (text[n] && pg.said[i][n] == text[n]) ++n;
    if (text[n] == 0 && pg.said[i][n] == 0) return;                // the same text: not set again
    if (op1f_text_set(pg.doc, SAID_PART[i], text, 0, SAID_ALIGN[i]) == 0) { pg.owed = 1; return; }   // again next frame
    n = 0;
    while (text[n] && n + 1u < (u32)SAID_BYTES) { pg.said[i][n] = text[n]; ++n; }
    pg.said[i][n] = 0;
}

static void say_value(int i, int v) {
    char t[8];
    if (v < 0) put_text(t, "-"); else put_number(t, (u32)v);
    say(i, t);
}

static void move(void* part, int a, int b, int c, int d, int e, int f) {
    const int m[9] = {a, b, 0, c, d, 0, e, f, 1024};
    op1f_move(part, m);
}

// the big digits of a value 0..127, or the dash: three groups (hundreds, tens, units), each changed only when it does
static void digits(void** set, int* at, int v) {
    int now[3] = {-1, -1, D_DASH};
    if (v >= 0) {
        int u = v, tens = 0;
        if (u >= 100) { u -= 100; now[0] = D_H1; }
        while (u >= 10) { u -= 10; ++tens; }
        if (v >= 10) now[1] = D_T0 + tens;
        now[2] = D_U0 + u;
    }
    for (int g = 0; g < 3; ++g) {
        if (now[g] == at[g]) continue;
        if (at[g] >= 0) op1f_show(set[at[g]], 0);
        if (now[g] >= 0) op1f_show(set[now[g]], 1);
        at[g] = now[g];
    }
}

static u32 put_tenths(char* out, int ms) {            // ms as seconds with one decimal: tenths (ms * 5243) >> 19
    u32 t = ((u32)ms * 5243u) >> 19, whole = 0;
    while (t >= 10u) { t -= 10u; ++whole; }
    u32 at = put_number(out, whole);
    out[at++] = '.';
    out[at++] = (char)('0' + t);
    out[at] = 0;
    return at;
}

// half a beat in ms, round(50000 / centihz), doubled above 4 Hz (every second beat shown): by subtraction, once a change
static u32 half_beat(u32 chz) {
    if (chz == 0u) return 0u;
    u32 r = 50000u + (chz >> 1), q = 0;
    while (r >= chz) { r -= chz; ++q; }
    return chz > 400u ? q << 1 : q;
}

static void blades(int m, int mallet, int strike) {
    int h[GS_PARTIALS];
    int n = partial_heights(m, mallet, strike, h);
    for (int k = 0; k < BLADES; ++k) {
        const int on = k < n;
        if (on != pg.p_on[k]) { op1f_show(pg.p[k], on); pg.p_on[k] = on; }
        if (!on) continue;
        const int s = (h[k] * 910) >> 6, e = BLADE_X * (gs_partial_octaves(m, k) + 256);
        if (s == pg.p_s[k] && e == pg.p_e[k]) continue;              // moved only when its height or place changed
        pg.p_s[k] = s; pg.p_e[k] = e;
        move(pg.p[k], 1024, 0, 0, s, e, BLADE_Y * (1024 - s));
        if (k < TWIN_BLADES) { const int st = (s * 13) >> 4; move(pg.t[k], 1024, 0, 0, st, e, BLADE_Y * (1024 - st)); }
    }
    pg.blades_n = n;
}

static void twin_blades(int on) {                    // the red blades: with ombak, as many as the material has partials
    const int n = !on || pg.blades_n < 0 ? 0 : pg.blades_n < TWIN_BLADES ? pg.blades_n : TWIN_BLADES;
    for (int k = 0; k < TWIN_BLADES; ++k) if ((k < n) != (k < pg.t_n)) op1f_show(pg.t[k], k < n);
    pg.t_n = n;
}

// every text from the values (UNREAD: "-"); say() sets only those that changed
static void page_texts(const int* v, int off) {
    char t[SAID_BYTES];
    pg.owed = 0;
    say(S_V0, v[MATERIAL] < 0 ? "-" : gs_material_name(v[MATERIAL]));
    say_value(S_V4, v[DAMP]);
    say_value(S_V5, v[STRIKE]);
    say_value(S_V6, v[CLICK]);
    u32 at = v[DECAY] < 0 ? put_text(t, "-") : put_tenths(t, gs_decay_ms(v[DECAY]));      // held / after key up
    at += put_text(t + at, "/");
    at += v[DECAY] < 0 || v[DAMP] < 0 ? put_text(t + at, "-") : put_tenths(t + at, gs_damped_ms(v[DECAY], v[DAMP]));
    put_text(t + at, " s");
    say(S_RING, t);
    const int om = v[OMBAK];
    if (om < 0) put_text(t, "-");
    else if (om == 0) put_text(t, "off");
    else put_text(t + put_hundredths(t, (u32)gs_ombak_centihz(om)), " hz");
    say(S_V3, t);
    if (v[WIDTH] < 0) put_text(t, "-"); else put_number(t, (u32)v[WIDTH]);
    say(S_V7, om == 0 ? "" : t);                                     // width dimmed while ombak is off
    say(S_V7_DIM, om == 0 ? t : "");
    say(S_STATE, off ? "switched off" : "");
}

// the mallet and its hit: at rest (the spot's matrix), or swung (SWING[mallet >> 5], f += the spot's) while frames of a
// swing are left; moved and shown only when the pose, or the strike value it sits at, changed. Strike unread: left as is.
static void mallet_pose(int mallet, int strike) {
    const int pose = pg.swing_left > 0 && mallet >= 0 ? mallet >> 5 : (int)REST;
    if (strike >= 0 && (pose != pg.mal_pose || strike != pg.mal_strike)) {
        if (pose == REST) move(pg.mallet, 1024, 0, 0, 1024, 0, SPOT_STEP * strike);
        else { const int* s = SWING[pose]; move(pg.mallet, s[0], s[1], s[3], s[4], s[6], s[7] + SPOT_STEP * strike); }
        pg.mal_pose = pose; pg.mal_strike = strike;
    }
    const int hit = pg.mal_pose != REST;
    if (hit != pg.hit_on) { op1f_show(pg.hit, hit); pg.hit_on = hit; }
}

// the page down, or not the picture's frame: strikes meanwhile are not swung, and a swing ends at rest
static void strike_idle() {
    pg.strikes_at = heard_strikes;
    pg.swing_left = 0;
    mallet_pose(0, pg.mal_strike);
}

static void page_update(u32 where, u32 how, u32 ms, u32 mode) {
    int v[VALUES], changed = 0;
    for (u32 i = 0; i < (u32)VALUES; ++i) {
        int x = 0;
        if (op1f_param_get(&op1f_card_gangsa, ASK, where, i, &x) != 0) x = UNREAD;
        else if (x < 0) x = 0;
        else if (x > (i == (u32)MATERIAL ? GS_MATERIALS - 1 : GS_TOP)) x = i == (u32)MATERIAL ? GS_MATERIALS - 1 : GS_TOP;
        v[i] = x;
        if (x != pg.last[i]) changed |= 1 << i;
    }
    const int m = v[MATERIAL], om = v[OMBAK];
    if (changed & (1 << MATERIAL)) {
        if (m < 0) { if (pg.needle_on != 0) { op1f_show(pg.needle, 0); pg.needle_on = 0; } }
        else {
            if (pg.needle_on != 1) { op1f_show(pg.needle, 1); pg.needle_on = 1; }
            op1f_move(pg.needle, NEEDLE[m]);
        }
        if (pg.lit >= 0) op1f_show(pg.md[pg.lit], 0);
        if (m >= 0) op1f_show(pg.md[m], 1);
        pg.lit = m;
    }
    if (changed & (1 << MALLET)) digits(pg.mal, pg.mal_at, v[MALLET]);
    if (changed & (1 << DECAY)) digits(pg.dec, pg.dec_at, v[DECAY]);
    if ((changed & (1 << STRIKE)) && v[STRIKE] >= 0) {               // centre (0) to the key's end (-15 px)
        move(pg.spot, 1024, 0, 0, 1024, 0, SPOT_STEP * v[STRIKE]);
        move(pg.hit, 1024, 0, 0, 1024, 0, SPOT_STEP * v[STRIKE]);     // the hit at the spot, shown or not
    }
    const int swings = mode == (u32)MODE_SYNTH && !(how & (u32)OP1F_SHOWN_OFF);   // the synth's page, not switched off
    if (pg.swing_left > 0) --pg.swing_left;
    const u32 heard = heard_strikes;                                  // one read: a note may come meanwhile
    if (!swings) pg.swing_left = 0;
    else if (heard != pg.strikes_at) { pg.strikes_seen += heard - pg.strikes_at; pg.swing_left = SWING_FRAMES; }   // restarts
    pg.strikes_at = heard;
    mallet_pose(v[MALLET], v[STRIKE]);
    if ((changed & ((1 << MATERIAL) | (1 << MALLET) | (1 << STRIKE))) && m >= 0 && v[MALLET] >= 0 && v[STRIKE] >= 0) {
        blades(m, v[MALLET], v[STRIKE]);
        twin_blades(pg.twin_on == 1);                                 // a material with another count of partials
    }
    if (changed & (1 << OMBAK)) {
        int n = 0;
        while (n < METER && om >= (int)METER_AT[n]) ++n;
        for (int k = 0; k < METER; ++k) if ((k < n) != (k < pg.lit_bars)) op1f_show(pg.om[k], k < n);
        pg.lit_bars = n;
        const int dim = om == 0;                                       // WIDTH heard only with ombak
        if (dim != pg.dim) { op1f_show(pg.wlab, !dim); op1f_show(pg.wlab_dim, dim); pg.dim = dim; }
        const int on = om > 0;
        if (on != pg.twin_on) {
            op1f_show(pg.twin, on);
            twin_blades(on);
            pg.twin_on = on;
            if (on) { pg.sign = 1; pg.sway_at = ms; move(pg.twin, 1024, 0, 0, 1024, SWAY, 0); }
        }
        if (on) pg.half = half_beat((u32)gs_ombak_centihz(om));
    }
    if (pg.twin_on == 1 && pg.half && ms - pg.sway_at >= pg.half) {   // the twin sways a half beat each way
        pg.sway_at = ms - pg.sway_at >= 2u * pg.half ? ms : pg.sway_at + pg.half;
        pg.sign = -pg.sign;
        move(pg.twin, 1024, 0, 0, 1024, pg.sign * SWAY, 0);
    }
    for (int i = 0; i < VALUES; ++i) pg.last[i] = v[i];
    const int off = (how & (u32)OP1F_SHOWN_OFF) != 0;
    if (off != pg.off) { op1f_show(pg.art, !off); pg.off = off; changed |= 1 << VALUES; }   // the instrument hidden while off
    if (changed || pg.owed) page_texts(v, off);
}

extern "C" void gangsa_frame(const Op1fFrame* now) OP1F_NAMED("gangsa_frame");
extern "C" void gangsa_frame(const Op1fFrame* now) {
    if (now->bytes < (u32)FRAME_LEVEL_4) return;
    frames_counted = frames_counted + 1u;                     // version 6: render's clock for a voice at rest
    if (!pg.asked) { pg.asked = 1; make_page(); }              // the picture is asked for once; 0 is kept
    if (!page.stood) {                                        // the picture, or the base's blank page to paint on
        if (op1f_page_stand_in(&op1f_card_gangsa, ASK, pg.doc)) page.stood = 1;
        else if (!page.refusal_said) {
            page.refusal_said = 1;
            op1f_log(2, "../src/main/gangsa.cpp", 1, "gangsa: the base did not take its page; T1 shows the base's blank page with its name");
        }
    }
    u32 how = shown_how, where = shown_where;
    if (now->page != &op1f_card_gangsa || !(how & (u32)OP1F_SHOWN_UP)) {
        if (pg.doc && page.stood) strike_idle();                 // the page left: the mallet back at rest
        if (pg.up && pg.strikes_seen != pg.strikes_said) {       // one line as the page leaves, if notes were heard on it
            pg.strikes_said = pg.strikes_seen;
            op1f_log(3, "../src/main/gangsa.cpp", 1, "gangsa: %u notes heard on the page so far", pg.strikes_seen);
        }
        pg.up = 0;
        return;
    }
    pg.up = 1;
    if (pg.doc && page.stood) page_update(where, how, now->ms, now->mode);   // the picture, once the base has taken it
    else if (now->canvas != 0) paint(now->canvas, where, how);      // else the blank page standing in is painted
}
