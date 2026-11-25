/***************************************************************************
    Yamaha YM2151 driver (version 2.150 final beta) - May, 11th 2002
    
    (c) 1997-2002 Jarek Burczynski (s0246@poczta.onet.pl, bujar@mame.net)
    Some of the optimizing ideas by Tatsuyuki Satoh
    
    This driver is based upon the MAME source code, with some minor 
    modifications to integrate it into the Cannonball framework. 
    
    See http://mamedev.org/source/docs/license.txt for more details.
***************************************************************************/

#include <cstdlib>
#include <cmath>
#include <cstring>

#include "hwaudio/ym2151.hpp"

signed int     chanout[8];
signed int     m2, c1, c2;            /* Phase Modulation input for operators 2,3,4  */
signed int     mem;                 /* one sample delay memory */

YM2151Operator oper[32];            /* the 32 operators */

uint32_t       pan[16];             /* channels output masks (0xffffffff = enable) */

uint32_t       eg_cnt;              /* global envelope generator counter */
uint32_t       eg_timer;            /* global envelope generator counter works at frequency = chipclock/64/3 */
uint32_t       eg_timer_add;        /* step of eg_timer */
uint32_t       eg_timer_overflow;   /* envelope generator timer overflows every 3 samples (on real chip) */

uint32_t       lfo_phase;           /* accumulated LFO phase (0 to 255) */
uint32_t       lfo_timer;           /* LFO timer                        */
uint32_t       lfo_timer_add;       /* step of lfo_timer                */
uint32_t       lfo_overflow;        /* LFO generates new output when lfo_timer reaches this value */
uint32_t       lfo_counter;         /* LFO phase increment counter      */
uint32_t       lfo_counter_add;     /* step of lfo_counter              */
uint8_t        lfo_wsel;            /* LFO waveform (0-saw, 1-square, 2-triangle, 3-random noise) */
uint8_t        amd;                 /* LFO Amplitude Modulation Depth   */
int8_t         pmd;                 /* LFO Phase Modulation Depth       */
uint32_t       lfa;                 /* LFO current AM output            */
int32_t        lfp;                 /* LFO current PM output            */

uint8_t        test;                /* TEST register */
uint8_t        ct;                  /* output control pins (bit1-CT2, bit0-CT1) */

uint32_t       noise;               /* noise enable/period register (bit 7 - noise enable, bits 4-0 - noise period */
uint32_t       noise_rng;           /* 17 bit noise shift register */
uint32_t       noise_p;             /* current noise 'phase'*/
uint32_t       noise_f;             /* current noise period */

uint32_t       csm_req;             /* CSM  KEY ON / KEY OFF sequence request */

uint32_t       irq_enable;          /* IRQ enable for timer B (bit 3) and timer A (bit 2); bit 7 - CSM mode (keyon to all slots, everytime timer A overflows) */
uint32_t       status;              /* chip status (BUSY, IRQ Flags) */
uint8_t        connects[8];         /* channels connections */

#ifdef USE_MAME_TIMERS
    emu_timer  *timer_A;
    emu_timer  *timer_B;
    attotime   timer_A_time[1024];  /* timer A times for MAME */
    attotime   timer_B_time[256];   /* timer B times for MAME */
    int        irqlinestate;
#else
    uint8_t    tim_A;               /* timer A enable (0-disabled) */
    uint8_t    tim_B;               /* timer B enable (0-disabled) */
    int32_t    tim_A_val;           /* current value of timer A */
    int32_t    tim_B_val;           /* current value of timer B */
    uint32_t   tim_A_tab[1024];     /* timer A deltas */
    uint32_t   tim_B_tab[256];      /* timer B deltas */
#endif
uint32_t       timer_A_index;       /* timer A index */
uint32_t       timer_B_index;       /* timer B index */
uint32_t       timer_A_index_old;   /* timer A previous index */
uint32_t       timer_B_index_old;   /* timer B previous index */

uint32_t       freq[11 * 768];      /* 11 octaves, 768 'cents' per octave */
int32_t        dt1_freq[8 * 32];    /* 8 DT1 levels, 32 KC values */
uint32_t       noise_tab[32];       /* 17bit Noise Generator periods */

#ifndef M_PI
#define M_PI             3.14159265358979323846
#endif

#define FREQ_SH          16  /* 16.16 fixed point (frequency calculations) */
#define EG_SH            16  /* 16.16 fixed point (envelope generator timing) */
#define LFO_SH           10  /* 22.10 fixed point (LFO calculations)       */
#define TIMER_SH         16  /* 16.16 fixed point (timers calculations)    */

#define FREQ_MASK        ((1 << FREQ_SH) - 1)

#define ENV_BITS         10
#define ENV_LEN          (1 << ENV_BITS)
#define ENV_STEP         (128.0 / ENV_LEN)

#define MAX_ATT_INDEX    (ENV_LEN - 1) /* 1023 */
#define MIN_ATT_INDEX    (0)           /* 0 */

#define EG_ATT           4
#define EG_DEC           3
#define EG_SUS           2
#define EG_REL           1
#define EG_OFF           0

#define SIN_BITS         10
#define SIN_LEN          (1 << SIN_BITS)
#define SIN_MASK         (SIN_LEN - 1)

#define TL_RES_LEN       (256) /* 8 bits addressing (real chip) */

/* 16-Bit Sample Setup */
#define FINAL_SH         (0)
#define MAXOUT           (+32767)
#define MINOUT           (-32768)

#define TL_TAB_LEN (13 * 2 * TL_RES_LEN)
static signed int tl_tab[TL_TAB_LEN];

#define ENV_QUIET        (TL_TAB_LEN >> 3)

static unsigned int sin_tab[SIN_LEN];
static uint32_t d1l_tab[16];
static bool tables_initialized = false;

#define RATE_STEPS (8)
static const uint8_t eg_inc[19 * RATE_STEPS] = {
    /* 0 */ 0, 1, 0, 1, 0, 1, 0, 1,
    /* 1 */ 0, 1, 0, 1, 1, 1, 0, 1,
    /* 2 */ 0, 1, 1, 1, 0, 1, 1, 1,
    /* 3 */ 0, 1, 1, 1, 1, 1, 1, 1,

    /* 4 */ 1, 1, 1, 1, 1, 1, 1, 1,
    /* 5 */ 1, 1, 1, 2, 1, 1, 1, 2,
    /* 6 */ 1, 2, 1, 2, 1, 2, 1, 2,
    /* 7 */ 1, 2, 2, 2, 1, 2, 2, 2,

    /* 8 */ 2, 2, 2, 2, 2, 2, 2, 2,
    /* 9 */ 2, 2, 2, 4, 2, 2, 2, 4,
    /*10 */ 2, 4, 2, 4, 2, 4, 2, 4,
    /*11 */ 2, 4, 4, 4, 2, 4, 4, 4,

    /*12 */ 4, 4, 4, 4, 4, 4, 4, 4,
    /*13 */ 4, 4, 4, 8, 4, 4, 4, 8,
    /*14 */ 4, 8, 4, 8, 4, 8, 4, 8,
    /*15 */ 4, 8, 8, 8, 4, 8, 8, 8,

    /*16 */ 8, 8, 8, 8, 8, 8, 8, 8,
    /*17 */ 16, 16, 16, 16, 16, 16, 16, 16,
    /*18 */ 0, 0, 0, 0, 0, 0, 0, 0,
};

#define O(a) ((a) * RATE_STEPS)

static const uint8_t eg_rate_select[32 + 64 + 32] = {
    O(18), O(18), O(18), O(18), O(18), O(18), O(18), O(18),
    O(18), O(18), O(18), O(18), O(18), O(18), O(18), O(18),
    O(18), O(18), O(18), O(18), O(18), O(18), O(18), O(18),
    O(18), O(18), O(18), O(18), O(18), O(18), O(18), O(18),

    O( 0), O( 1), O( 2), O( 3),
    O( 0), O( 1), O( 2), O( 3),
    O( 0), O( 1), O( 2), O( 3),
    O( 0), O( 1), O( 2), O( 3),
    O( 0), O( 1), O( 2), O( 3),
    O( 0), O( 1), O( 2), O( 3),
    O( 0), O( 1), O( 2), O( 3),
    O( 0), O( 1), O( 2), O( 3),
    O( 0), O( 1), O( 2), O( 3),
    O( 0), O( 1), O( 2), O( 3),
    O( 0), O( 1), O( 2), O( 3),
    O( 0), O( 1), O( 2), O( 3),

    O( 4), O( 5), O( 6), O( 7),
    O( 8), O( 9), O(10), O(11),
    O(12), O(13), O(14), O(15),
    O(16), O(16), O(16), O(16),

    O(16), O(16), O(16), O(16), O(16), O(16), O(16), O(16),
    O(16), O(16), O(16), O(16), O(16), O(16), O(16), O(16),
    O(16), O(16), O(16), O(16), O(16), O(16), O(16), O(16),
    O(16), O(16), O(16), O(16), O(16), O(16), O(16), O(16)
};
#undef O

#define O(a) (a)
static const uint8_t eg_rate_shift[32 + 64 + 32] = {
    O(0), O(0), O(0), O(0), O(0), O(0), O(0), O(0),
    O(0), O(0), O(0
