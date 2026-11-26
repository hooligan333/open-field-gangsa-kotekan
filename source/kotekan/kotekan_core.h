// kotekan: the portable core of the sequencer (pack/kotekan.cpp), the same code on the computer (core/test, host/)
// and on the unit. No includes, no library, no division, no 64-bit arithmetic, no floating point, no constructor,
// no struct copy. Calls nothing: the pack makes the note calls the core decides (KcActions).
//
// The player holds a key: the pokok. Its note is snapped to the nearest key of the MAP (five keys an octave) and
// counted as a degree of the map (five to an octave). While a key is held the figure runs in steps, RATE steps to
// the tape's beat, ON THE TAPE'S GRID: the tick's `pulse` (0..47, an eighth, back to 0 when the tape starts) and its
// wraps give a phase of four beats (0..383 pulses); a step falls where the phase is a multiple of the step's pulses
// (KC_RATE_PULSES), whatever the moment of the key or of a rate change. The figure's own cycle is KC_RATE_CYCLE steps
// (4, or 6 at 3 and 6 a beat) and always starts on a tape beat (at 8 a beat, on a beat or the half beat). The pokok
// changes only on the cycle's first step (k = 0, the arrival); a key pressed during a cycle is the NEXT pokok, which
// the styles head for (core/DATA.md says which forms are sourced and which are reconstructions).
// Each figure note lasts one step: all figure notes end at the next step, before that step's notes start. The
// pokok (parts "both + pokok") sounds an octave below the held key's map note from k = 0 until the next k = 0.
// Unisons of the two parts (norot k = 1, telu's pivot) are one note at the polos velocity. All keys up: every note
// ends at once (kc_key_event); a key pressed before the next step carries the figure on toward it, else the figure
// ends at that step. At most 3 notes sound at once (KC_SOUNDING room is 4).
#ifndef KOTEKAN_CORE_H
#define KOTEKAN_CORE_H

typedef unsigned int kc_u32;
typedef unsigned char kc_u8;
typedef signed char kc_s8;

enum { KC_STYLES = 4, KC_MAPS = 4, KC_RATES = 5, KC_PARTS = 4, KC_HELD = 8, KC_SOUNDING = 4, KC_REST = -1000,
       KC_SHOWN = 12, KC_SHOWN_ZERO = 32, KC_PHASE = 384 };
enum { KC_NYOGCAG = 0, KC_NOROT = 1, KC_TELU = 2, KC_EMPAT = 3 };
enum { KC_BOTH = 0, KC_POLOS_ONLY = 1, KC_SANGSIH_ONLY = 2, KC_BOTH_POKOK = 3 };
enum { KC_ROLE_FIGURE = 0, KC_ROLE_POKOK = 1 };
enum { KC_DEFAULT_STYLE = KC_TELU, KC_DEFAULT_MAP = 0, KC_DEFAULT_RATE = 2, KC_DEFAULT_PARTS = KC_BOTH };

// The maps: the five keys of an octave, as pitch classes from C, rising. 0 "c d e g a" (pelog selisir on the white
// keys; slendro on C D E G A), 1 "f g a c d" (pelog tembung), 2 "g a b d e" (pelog sunaren), 3 the black keys.
static const kc_u8 KC_MAP_PC[KC_MAPS][5] = { { 0, 2, 4, 7, 9 }, { 0, 2, 5, 7, 9 }, { 2, 4, 7, 9, 11 }, { 1, 3, 6, 8, 10 } };
// The rates: steps to the tape's beat, the pulses a step at 96 to a quarter note (96 / steps, written out), and the
// steps of the figure's cycle: 4 (2 beats at 2, 1 at 4, half a beat at 8) or 6 (2 beats at 3, 1 at 6).
static const kc_u8 KC_RATE_STEPS[KC_RATES] = { 2, 3, 4, 6, 8 };
static const kc_u8 KC_RATE_PULSES[KC_RATES] = { 48, 32, 24, 16, 12 };
static const kc_u8 KC_RATE_CYCLE[KC_RATES] = { 4, 6, 4, 6, 4 };

// The settings, one word: style | map << 4 | rate (index) << 8 | parts << 12. Out-of-range fields read as the default.
static inline kc_u32 kc_settings(kc_u32 style, kc_u32 map, kc_u32 rate, kc_u32 parts) {
    return (style & 15u) | (map & 15u) << 4 | (rate & 15u) << 8 | (parts & 15u) << 12;
}
static inline kc_u32 kc_style(kc_u32 s) { kc_u32 v = s & 15u; return v < (kc_u32)KC_STYLES ? v : (kc_u32)KC_DEFAULT_STYLE; }
static inline kc_u32 kc_map(kc_u32 s) { kc_u32 v = s >> 4 & 15u; return v < (kc_u32)KC_MAPS ? v : (kc_u32)KC_DEFAULT_MAP; }
static inline kc_u32 kc_rate(kc_u32 s) { kc_u32 v = s >> 8 & 15u; return v < (kc_u32)KC_RATES ? v : (kc_u32)KC_DEFAULT_RATE; }
static inline kc_u32 kc_parts(kc_u32 s) { kc_u32 v = s >> 12 & 15u; return v < (kc_u32)KC_PARTS ? v : (kc_u32)KC_DEFAULT_PARTS; }

// ---- degrees of a map
// A note's degree: the nearest key of the map (a tie goes to the lower key), counted five to an octave from the C
// of octave 0. Every note 0..127 has one whose note lies in 0..127.
static inline int kc_degree(kc_u32 map, int note) {
    if (map >= (kc_u32)KC_MAPS) map = 0;
    if (note < 0) note = 0;
    if (note > 127) note = 127;
    int oct = 0, r = note;
    while (r >= 12) { r -= 12; ++oct; }
    int best = -1, best_d = 1000, best_n = 0;
    for (int o = -1; o <= 1; ++o)
        for (int i = 0; i < 5; ++i) {
            int n = (oct + o) * 12 + KC_MAP_PC[map][i];
            if (n < 0 || n > 127) continue;
            int d = n > note ? n - note : note - n;
            if (d < best_d || (d == best_d && n < best_n)) { best_d = d; best_n = n; best = (oct + o) * 5 + i; }
        }
    return best;
}
// A degree's note, or -1 beyond 0..127.
static inline int kc_note(kc_u32 map, int degree) {
    if (map >= (kc_u32)KC_MAPS) map = 0;
    if (degree < 0) return -1;
    int oct = 0;
    while (degree >= 5) { degree -= 5; ++oct; }
    int n = oct * 12 + KC_MAP_PC[map][degree];
    return n > 127 ? -1 : n;
}

// ---- the figure: the styles' tables (core/DATA.md holds the same rows)
// Nyog cag: the composite, one tone a step, in degrees above the beat's pokok P, for each cycle length (4, 6) and
// for the next pokok N - P clamped to -2..+2 (row 0 = -2 ... row 4 = +2). Polos plays the even steps, sangsih the
// odd ones: the two never strike together, and the composite moves by one degree a step inside a cycle.
static const kc_s8 KC_NYOG4[5][4] = { { 0, 1, 0, -1 }, { 0, 1, 0, -1 }, { 0, 1, 2, 1 }, { 0, 1, 2, 1 }, { 0, 1, 2, 3 } };
static const kc_s8 KC_NYOG6[5][6] = { { 0, 1, 0, -1, -2, -1 }, { 0, 1, 2, 1, 0, -1 }, { 0, 1, 2, 3, 2, 1 },
                                      { 0, 1, 2, 3, 2, 1 }, { 0, 1, 2, 3, 4, 3 } };
// Norot: for each step, what polos and sangsih strike: 0 rest, 1 the pokok P, 2 A (the next tone, or P + 1 held).
static const kc_u8 KC_NOROT4[2][4] = { { 1, 2, 0, 2 }, { 0, 2, 1, 0 } };
static const kc_u8 KC_NOROT6[2][6] = { { 1, 2, 0, 2, 0, 2 }, { 0, 2, 1, 0, 1, 0 } };
// Telu and empat: what polos and sangsih strike, in degrees above the cell c, for each role of the walk (0, 1, 2).
static const kc_s8 KC_TELU_ROLE[3][2] = { { 0, -1 }, { 1, 1 }, { -1, 2 } };
static const kc_s8 KC_EMPAT_ROLE[3][2] = { { 0, 3 }, { 1, -1 }, { -1, 2 } };
// The cell of telu and empat (cell, pos, dir) runs at every step whatever the style, so a change of style mid-figure
// carries on from where the cell is. Telu: three adjacent degrees cell..cell+2, the composite walking them as a
// cycle (pos 0 1 2 0 ... rising, 2 1 0 2 ... falling, dir the way the next pokok lies); polos takes the lower two,
// sangsih the upper two, both strike the middle (the pivot). Empat: the same walk; polos as telu's, sangsih the upper
// two of cell..cell+3, and on polos's lowest tone sangsih strikes cell+3 with it (the kempyung). The cell may lie
// up to two degrees below the pokok (the pokok is then its middle or top tone, as the sourced telu row has it).
struct KcFig { int cell, pos, dir, init; };
struct KcStep { int polos, sangsih; };          // degrees, or KC_REST

static inline int kc_fit(int cell, int lo, int hi) {   // the cell moved as little as it takes to hold lo..hi
    if (lo < cell) cell = lo;
    if (hi > cell + 2) cell = hi - 2;
    return cell;
}

// k: the step in the figure's cycle, 0..n-1 (n 4 or 6). cur: the pokok's degree (the cycle's). next: the degree of
// the key held now (the next pokok; cur when the same). arrived: 1 at the k = 0 where cur became a new tone.
static inline void kc_figure(KcFig* f, kc_u32 style, kc_u32 k, kc_u32 n, int cur, int next, int arrived, KcStep* o) {
    if (n != 6u) n = 4u;
    if (k >= n) k = 0;
    if (!f->init) {
        f->init = 1; f->cell = cur; f->pos = 0; f->dir = 1;
    } else {
        if (next != cur) f->dir = next > cur ? 1 : -1;
        int cell = f->cell, pos = f->pos;      // pos is the role (0 polos, 1 both, 2 sangsih): kept when the cell moves
        if (k == 0) cell = kc_fit(cell, cur, cur);
        else if (next != cur) {
            int lo = next < cur ? next : cur, hi = next < cur ? cur : next;
            cell = hi - lo <= 2 ? kc_fit(cell, lo, hi) : kc_fit(cell, next, next);
        }
        int np = pos + f->dir;                 // never the role of the step before: no part rests twice in a row
        if (np > 2) np = 0;
        if (np < 0) np = 2;
        if (k == 0 && arrived) {               // land on the new pokok: the cell nearest that holds it in another role
            int best = 0, gap = 1000;
            for (int r = 0; r <= 2; ++r) {
                if (r == pos) continue;
                int d = cur - r - f->cell;
                if (d < 0) d = -d;
                if (d < gap) { gap = d; best = r; }
            }
            cell = cur - best; np = best;
        }
        f->cell = cell; f->pos = np;
    }
    int c = f->cell, p = f->pos;
    o->polos = KC_REST; o->sangsih = KC_REST;
    switch (style) {
    case KC_NYOGCAG: {
        int d = next - cur;
        d = d < -2 ? -2 : d > 2 ? 2 : d;
        int tone = cur + (n == 6u ? KC_NYOG6[d + 2][k] : KC_NYOG4[d + 2][k]);
        if (!(k & 1u)) o->polos = tone; else o->sangsih = tone;
        break;
    }
    case KC_NOROT: {
        int a = next != cur ? next : cur + 1;
        kc_u32 wp = n == 6u ? KC_NOROT6[0][k] : KC_NOROT4[0][k], ws = n == 6u ? KC_NOROT6[1][k] : KC_NOROT4[1][k];
        if (wp) o->polos = wp == 1u ? cur : a;
        if (ws) o->sangsih = ws == 1u ? cur : a;
        break;
    }
    default: {                                   // KC_TELU, KC_EMPAT
        const kc_s8* r = style == (kc_u32)KC_EMPAT ? KC_EMPAT_ROLE[p] : KC_TELU_ROLE[p];
        if (r[0] >= 0) o->polos = c + r[0];
        if (r[1] >= 0) o->sangsih = c + r[1];
        break;
    }
    }
}

// ---- the player: keys held, the clock, the notes sounding
struct KcHeld { kc_u8 from, place, note, velocity; };
struct KcSound { kc_u8 note, role; };
struct KcActions {                               // what the pack calls after its masked section: offs, then ons
    kc_u32 epoch, offs, ons;
    kc_u8 off[KC_SOUNDING];
    kc_u8 on[KC_SOUNDING], on_velocity[KC_SOUNDING];
};
struct KcPlayer {
    KcHeld held[KC_HELD]; kc_u32 holding;        // keys held, oldest first; the last is the pokok
    kc_u32 running, k, slot;                     // a figure playing; its step in the cycle; its place on the page
    kc_u32 counted, last_count, last_pulse;      // the clock: the last tick's count and pulse
    kc_u32 phase, eighth, last_idx, last_len;    // the grid: pulses into four beats, its eighth, the last step's place
    int cur, cur_map; kc_u32 velocity;
    KcFig fig;
    KcSound sound[KC_SOUNDING]; kc_u32 sounding; // the notes this player has on
    kc_u32 epoch;                                // bumped by every stop and every silence: a decision made before is stale
    kc_u32 steps, starts, skipped;               // counts (skipped: steps that came late, ticks missed)
    // what the page shows, single words: the steps of two cycles (index slot, shown_n of them), each
    // polos | sangsih << 8 as degrees above the pokok + KC_SHOWN_ZERO, 0 a rest; the step's slot; the pokok's note + 1
    volatile kc_u32 shown[KC_SHOWN], shown_slot, shown_n, shown_pokok, shown_degree;
};

static inline void kc_reset(KcPlayer* p) {       // word by word: no memset
    p->holding = 0; p->running = 0; p->k = 0; p->slot = 0;
    p->counted = 0; p->last_count = 0; p->last_pulse = 0; p->phase = 0; p->eighth = 0; p->last_idx = 0; p->last_len = 0;
    p->cur = 0; p->cur_map = -1; p->velocity = 0; p->fig.init = 0; p->fig.cell = 0; p->fig.pos = 0; p->fig.dir = 1;
    p->sounding = 0; p->epoch = p->epoch + 1u;
    for (kc_u32 i = 0; i < (kc_u32)KC_SHOWN; ++i) p->shown[i] = 0;
    p->shown_slot = 0; p->shown_n = 8; p->shown_pokok = 0;
}

static inline void kc_actions_clear(const KcPlayer* p, KcActions* a) { a->epoch = p->epoch; a->offs = 0; a->ons = 0; }

// A key down or up (from 0 keys / 1 MIDI, place 0..127 = key + 53). An up of a key not held changes nothing.
static inline void kc_key(KcPlayer* p, int down, kc_u32 from, kc_u32 place, kc_u32 note, kc_u32 velocity) {
    kc_u32 j = 0;
    for (kc_u32 i = 0; i < p->holding; ++i) {    // the same key again: taken out first
        if (p->held[i].from == from && p->held[i].place == place) continue;
        if (j != i) { p->held[j].from = p->held[i].from; p->held[j].place = p->held[i].place; p->held[j].note = p->held[i].note; p->held[j].velocity = p->held[i].velocity; }
        ++j;
    }
    p->holding = j;
    if (!down || note > 127u || velocity == 0 || velocity > 127u) return;
    if (p->holding == (kc_u32)KC_HELD) {         // full: the oldest let go (its up then changes nothing)
        for (kc_u32 i = 1; i < (kc_u32)KC_HELD; ++i) { p->held[i - 1].from = p->held[i].from; p->held[i - 1].place = p->held[i].place; p->held[i - 1].note = p->held[i].note; p->held[i - 1].velocity = p->held[i].velocity; }
        --p->holding;
    }
    KcHeld* h = &p->held[p->holding++];
    h->from = (kc_u8)from; h->place = (kc_u8)place; h->note = (kc_u8)note; h->velocity = (kc_u8)velocity;
}

static inline int kc_has(const KcPlayer* p, int note) {
    for (kc_u32 i = 0; i < p->sounding; ++i) if (p->sound[i].note == note) return 1;
    return 0;
}
static inline void kc_end_role(KcPlayer* p, KcActions* a, int all, kc_u32 role) {   // into a->off, out of the list
    kc_u32 j = 0;
    for (kc_u32 i = 0; i < p->sounding; ++i) {
        if (all || p->sound[i].role == role) { if (a->offs < (kc_u32)KC_SOUNDING) a->off[a->offs++] = p->sound[i].note; continue; }
        p->sound[j].note = p->sound[i].note; p->sound[j].role = p->sound[i].role; ++j;
    }
    p->sounding = j;
}

// A key event as the pack gets it: the key taken in or out; when the last held key goes up, every note this player
// has on ends now (a decision made before is stale). The figure stays ready: a key pressed before the next step
// carries it on toward the new tone; at that step with no key held it ends.
static inline void kc_key_event(KcPlayer* p, int down, kc_u32 from, kc_u32 place, kc_u32 note, kc_u32 velocity, KcActions* a) {
    kc_actions_clear(p, a);
    kc_key(p, down, from, place, note, velocity);
    if (down || p->holding || !p->sounding) return;
    kc_end_role(p, a, 1, 0);
    p->epoch = p->epoch + 1u;
    a->epoch = p->epoch;
}
static inline void kc_start(KcPlayer* p, KcActions* a, int note, kc_u32 velocity, kc_u32 role) {
    if (note < 0 || note > 127 || kc_has(p, note) || p->sounding >= (kc_u32)KC_SOUNDING || a->ons >= (kc_u32)KC_SOUNDING) return;
    if (velocity < 1u) velocity = 1u;
    if (velocity > 127u) velocity = 127u;
    p->sound[p->sounding].note = (kc_u8)note; p->sound[p->sounding].role = (kc_u8)role; ++p->sounding;
    a->on[a->ons] = (kc_u8)note; a->on_velocity[a->ons] = (kc_u8)velocity; ++a->ons;
}

static inline kc_u32 kc_shown_word(int degree, int cur) {
    if (degree == KC_REST) return 0;
    int v = degree - cur + KC_SHOWN_ZERO;
    return v < 1 ? 1u : v > 255 ? 255u : (kc_u32)v;
}

// One step of the figure (k in the cycle of n, slot on the page): the last step's figure notes ended, this step's started.
static inline void kc_step(KcPlayer* p, kc_u32 settings, kc_u32 k, kc_u32 n, kc_u32 slot, KcActions* a) {
    kc_u32 style = kc_style(settings), map = kc_map(settings), parts = kc_parts(settings);
    const KcHeld* h = &p->held[p->holding - 1u];
    int pend = kc_degree(map, h->note);
    if ((int)map != p->cur_map) { p->cur_map = (int)map; p->cur = pend; p->fig.init = 0; }
    p->velocity = h->velocity;
    int arrived = 0;
    if (k == 0 && pend != p->cur) { p->cur = pend; arrived = 1; }
    KcStep s;
    kc_figure(&p->fig, style, k, n, p->cur, pend, arrived, &s);
    kc_end_role(p, a, 0, KC_ROLE_FIGURE);
    if (k == 0 || parts != (kc_u32)KC_BOTH_POKOK) kc_end_role(p, a, 0, KC_ROLE_POKOK);
    kc_u32 vp = p->velocity, vs = p->velocity - (p->velocity >> 3);            // sangsih an eighth softer
    if (k == 0 && parts == (kc_u32)KC_BOTH_POKOK) {
        int pn = kc_note(map, p->cur);
        if (pn >= 12) kc_start(p, a, pn - 12, vp, KC_ROLE_POKOK);
    }
    int pn = parts == (kc_u32)KC_SANGSIH_ONLY ? -1 : s.polos == KC_REST ? -1 : kc_note(map, s.polos);
    int sn = parts == (kc_u32)KC_POLOS_ONLY ? -1 : s.sangsih == KC_REST ? -1 : kc_note(map, s.sangsih);
    if (pn >= 0) kc_start(p, a, pn, vp, KC_ROLE_FIGURE);
    if (sn >= 0 && sn != pn) kc_start(p, a, sn, vs, KC_ROLE_FIGURE);
    if (slot >= (kc_u32)KC_SHOWN) slot = 0;
    p->shown[slot] = kc_shown_word(parts == (kc_u32)KC_SANGSIH_ONLY ? KC_REST : s.polos, p->cur) |
                     kc_shown_word(parts == (kc_u32)KC_POLOS_ONLY ? KC_REST : s.sangsih, p->cur) << 8;
    p->shown_degree = (kc_u32)p->cur;
    p->shown_pokok = (kc_u32)(kc_note(map, p->cur) + 1);
    p->shown_slot = slot;
    p->shown_n = n + n;
    p->k = k; p->slot = slot;
    ++p->steps;
}

// A tick. count: the sequencer's count of its ticks. pulse: the tick's pulse (0..47). restart: not 0 when the
// firmware set its count back (the tick's `jumped`) or the tape began to play: the grid starts again at this pulse.
// The phase follows `pulse` and its wraps; while `pulse` stands still it goes on by the count (missed ticks counted).
static inline void kc_tick(KcPlayer* p, kc_u32 settings, kc_u32 count, kc_u32 pulse, int restart, KcActions* a) {
    kc_actions_clear(p, a);
    if (pulse > 47u) pulse = 47u;
    kc_u32 gone = p->counted ? count - p->last_count : 1u;
    int fresh = !p->counted || restart;
    if (fresh) { p->eighth = 0; p->phase = pulse; }
    else if (pulse != p->last_pulse) {
        if (pulse < p->last_pulse) p->eighth = (p->eighth + 1u) & 7u;
        p->phase = p->eighth * 48u + pulse;
    } else {
        if (gone > 96u) gone = 96u;
        p->phase += gone;
        while (p->phase >= (kc_u32)KC_PHASE) p->phase -= (kc_u32)KC_PHASE;
        kc_u32 e = 0, r = p->phase;
        while (r >= 48u) { r -= 48u; ++e; }
        p->eighth = e;
    }
    p->counted = 1; p->last_count = count; p->last_pulse = pulse;
    kc_u32 rate = kc_rate(settings), len = KC_RATE_PULSES[rate], n = KC_RATE_CYCLE[rate];
    kc_u32 idx = 0, r = p->phase;
    while (r >= len) { r -= len; ++idx; }
    if (len != p->last_len) fresh = 1;           // a new rate: its first step on its own grid
    int step = fresh ? r == 0u : idx != p->last_idx;
    if (step && r) ++p->skipped;                 // the grid's pulse was missed: this step comes late
    p->last_idx = idx; p->last_len = len;
    if (!step) return;
    kc_u32 slot = idx, twice = n + n;
    while (slot >= twice) slot -= twice;
    kc_u32 k = slot >= n ? slot - n : slot;
    if (!p->running) {
        if (!p->holding) return;
        p->running = 1; p->fig.init = 0; p->cur_map = -1; ++p->starts;   // a figure begins at this step
        for (kc_u32 i = 0; i < (kc_u32)KC_SHOWN; ++i) p->shown[i] = 0;
        kc_step(p, settings, k, n, slot, a);
        return;
    }
    if (!p->holding) {                           // every key up: the figure ends at this step
        kc_end_role(p, a, 1, 0);
        p->running = 0;
        return;
    }
    if (slot == 0 || p->shown_n != twice) for (kc_u32 i = 0; i < (kc_u32)KC_SHOWN; ++i) p->shown[i] = 0;   // a new pair of cycles
    kc_step(p, settings, k, n, slot, a);
}

// Everything ended now (a stop of the pack's own). The figure stops; a decision made before is stale.
static inline void kc_silence(KcPlayer* p, KcActions* a) {
    kc_actions_clear(p, a);
    kc_end_role(p, a, 1, 0);
    p->running = 0;
    p->epoch = p->epoch + 1u;
    a->epoch = p->epoch;
}

// A note whose on the base refused: out of the list, if no stop or silence came since the decision.
static inline void kc_refused(KcPlayer* p, kc_u32 epoch, int note) {
    if (epoch != p->epoch) return;
    kc_u32 j = 0;
    for (kc_u32 i = 0; i < p->sounding; ++i) {
        if (p->sound[i].note == note) continue;
        p->sound[j].note = p->sound[i].note; p->sound[j].role = p->sound[i].role; ++j;
    }
    p->sounding = j;
}

#endif
