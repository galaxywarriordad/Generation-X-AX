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
    O(0), O(0), O(0), O(0), O(0), O(0), O(0), O(0),
    O(0), O(0), O(0), O(0), O(0), O(0), O(0), O(0),
    O(0), O(0), O(0), O(0), O(0), O(0), O(0), O(0),

    O(11), O(11), O(11), O(11),
    O(10), O(10), O(10), O(10),
    O( 9), O( 9), O( 9), O( 9),
    O( 8), O( 8), O( 8), O( 8),
    O( 7), O( 7), O( 7), O( 7),
    O( 6), O( 6), O( 6), O( 6),
    O( 5), O( 5), O( 5), O( 5),
    O( 4), O( 4), O( 4), O( 4),
    O( 3), O( 3), O( 3), O( 3),
    O( 2), O( 2), O( 2), O( 2),
    O( 1), O( 1), O( 1), O( 1),
    O( 0), O( 0), O( 0), O( 0),

    O( 0), O( 0), O( 0), O( 0),
    O( 0), O( 0), O( 0), O( 0),
    O( 0), O( 0), O( 0), O( 0),
    O( 0), O( 0), O( 0), O( 0),

    O( 0), O( 0), O( 0), O( 0), O( 0), O( 0), O( 0), O( 0),
    O( 0), O( 0), O( 0), O( 0), O( 0), O( 0), O( 0), O( 0),
    O( 0), O( 0), O( 0), O( 0), O( 0), O( 0), O( 0), O( 0),
    O( 0), O( 0), O( 0), O( 0), O( 0), O( 0), O( 0), O( 0)
};
#undef O

static const uint32_t dt2_tab[4] = { 0, 384, 500, 608 };

static const uint8_t dt1_tab[4 * 32] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

    0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2,
    2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 8, 8, 8, 8,

    1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5,
    5, 6, 6, 7, 8, 8, 9, 10, 11, 12, 13, 14, 16, 16, 16, 16,

    2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7,
    8, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 20, 22, 22, 22, 22
};

static const uint16_t phaseinc_rom[768] = {
    1299,1300,1301,1302,1303,1304,1305,1306,1308,1309,1310,1311,1313,1314,1315,1316,
    1318,1319,1320,1321,1322,1323,1324,1325,1327,1328,1329,1330,1332,1333,1334,1335,
    1337,1338,1339,1340,1341,1342,1343,1344,1346,1347,1348,1349,1351,1352,1353,1354,
    1356,1357,1358,1359,1361,1362,1363,1364,1366,1367,1368,1369,1371,1372,1373,1374,
    1376,1377,1378,1379,1381,1382,1383,1384,1386,1387,1388,1389,1391,1392,1393,1394,
    1396,1397,1398,1399,1401,1402,1403,1404,1406,1407,1408,1409,1411,1412,1413,1414,
    1416,1417,1418,1419,1421,1422,1423,1424,1426,1427,1429,1430,1431,1432,1434,1435,
    1437,1438,1439,1440,1442,1443,1444,1445,1447,1448,1449,1450,1452,1453,1454,1455,
    1458,1459,1460,1461,1463,1464,1465,1466,1468,1469,1471,1472,1473,1474,1476,1477,
    1479,1480,1481,1482,1484,1485,1486,1487,1489,1490,1492,1493,1494,1495,1497,1498,
    1501,1502,1503,1504,1506,1507,1509,1510,1512,1513,1514,1515,1517,1518,1520,1521,
    1523,1524,1525,1526,1528,1529,1531,1532,1534,1535,1536,1537,1539,1540,1542,1543,
    1545,1546,1547,1548,1550,1551,1553,1554,1556,1557,1558,1559,1561,1562,1564,1565,
    1567,1568,1569,1570,1572,1573,1575,1576,1578,1579,1580,1581,1583,1584,1586,1587,
    1590,1591,1592,1593,1595,1596,1598,1599,1601,1602,1604,1605,1607,1608,1609,1610,
    1613,1614,1615,1616,1618,1619,1621,1622,1624,1625,1627,1628,1630,1631,1632,1633,
    1637,1638,1639,1640,1642,1643,1645,1646,1648,1649,1651,1652,1654,1655,1656,1657,
    1660,1661,1663,1664,1666,1667,1669,1670,1672,1673,1675,1676,1678,1679,1681,1682,
    1685,1686,1688,1689,1691,1692,1694,1695,1697,1698,1700,1701,1703,1704,1706,1707,
    1709,1710,1712,1713,1715,1716,1718,1719,1721,1722,1724,1725,1727,1728,1730,1731,
    1734,1735,1737,1738,1740,1741,1743,1744,1746,1748,1749,1751,1752,1754,1755,1757,
    1759,1760,1762,1763,1765,1766,1768,1769,1771,1773,1774,1776,1777,1779,1780,1782,
    1785,1786,1788,1789,1791,1793,1794,1796,1798,1799,1801,1802,1804,1806,1807,1809,
    1811,1812,1814,1815,1817,1819,1820,1822,1824,1825,1827,1828,1830,1832,1833,1835,
    1837,1838,1840,1841,1843,1845,1846,1848,1850,1851,1853,1854,1856,1858,1859,1861,
    1864,1865,1867,1868,1870,1872,1873,1875,1877,1879,1880,1882,1884,1885,1887,1888,
    1891,1892,1894,1895,1897,1899,1900,1902,1904,1906,1907,1909,1911,1912,1914,1915,
    1918,1919,1921,1923,1925,1926,1928,1930,1932,1933,1935,1937,1939,1940,1942,1944,
    1946,1947,1949,1951,1953,1954,1956,1958,1960,1961,1963,1965,1967,1968,1970,1972,
    1975,1976,1978,1980,1982,1983,1985,1987,1989,1990,1992,1994,1996,1997,1999,2001,
    2003,2004,2006,2008,2010,2011,2013,2015,2017,2019,2021,2022,2024,2026,2028,2029,
    2032,2033,2035,2037,2039,2041,2043,2044,2047,2048,2050,2052,2054,2056,2058,2059,
    2062,2063,2065,2067,2069,2071,2073,2074,2077,2078,2080,2082,2084,2086,2088,2089,
    2092,2093,2095,2097,2099,2101,2103,2104,2107,2108,2110,2112,2114,2116,2118,2119,
    2122,2123,2125,2127,2129,2131,2133,2134,2137,2139,2141,2142,2145,2146,2148,2150,
    2153,2154,2156,2158,2160,2162,2164,2165,2168,2170,2172,2173,2176,2177,2179,2181,
    2185,2186,2188,2190,2192,2194,2196,2197,2200,2202,2204,2205,2208,2209,2211,2213,
    2216,2218,2220,2222,2223,2226,2227,2230,2232,2234,2236,2238,2239,2242,2243,2246,
    2249,2251,2253,2255,2256,2259,2260,2263,2265,2267,2269,2271,2272,2275,2276,2279,
    2281,2283,2285,2287,2288,2291,2292,2295,2297,2299,2301,2303,2304,2307,2308,2311,
    2315,2317,2319,2321,2322,2325,2326,2329,2331,2333,2335,2337,2338,2341,2342,2345,
    2348,2350,2352,2354,2355,2358,2359,2362,2364,2366,2368,2370,2371,2374,2375,2378,
    2382,2384,2386,2388,2389,2392,2393,2396,2398,2400,2402,2404,2407,2410,2411,2414,
    2417,2419,2421,2423,2424,2427,2428,2431,2433,2435,2437,2439,2442,2445,2446,2449,
    2452,2454,2456,2458,2459,2462,2463,2466,2468,2470,2472,2474,2477,2480,2481,2484,
    2488,2490,2492,2494,2495,2498,2499,2502,2504,2506,2508,2510,2513,2516,2517,2520,
    2524,2526,2528,2530,2531,2534,2535,2538,2540,2542,2544,2546,2549,2552,2553,2556,
    2561,2563,2565,2567,2568,2571,2572,2575,2577,2579,2581,2583,2586,2589,2590,2593
};

static const uint8_t lfo_noise_waveform[256] = {
    0xFF,0xEE,0xD3,0x80,0x58,0xDA,0x7F,0x94,0x9E,0xE3,0xFA,0x00,0x4D,0xFA,0xFF,0x6A,
    0x7A,0xDE,0x49,0xF6,0x00,0x33,0xBB,0x63,0x91,0x60,0x51,0xFF,0x00,0xD8,0x7F,0xDE,
    0xDC,0x73,0x21,0x85,0xB2,0x9C,0x5D,0x24,0xCD,0x91,0x9E,0x76,0x7F,0x20,0xFB,0xF3,
    0x00,0xA6,0x3E,0x42,0x27,0x69,0xAE,0x33,0x45,0x44,0x11,0x41,0x72,0x73,0xDF,0xA2,

    0x32,0xBD,0x7E,0xA8,0x13,0xEB,0xD3,0x15,0xDD,0xFB,0xC9,0x9D,0x61,0x2F,0xBE,0x9D,
    0x23,0x65,0x51,0x6A,0x84,0xF9,0xC9,0xD7,0x23,0xBF,0x65,0x19,0xDC,0x03,0xF3,0x24,
    0x33,0xB6,0x1E,0x57,0x5C,0xAC,0x25,0x89,0x4D,0xC5,0x9C,0x99,0x15,0x07,0xCF,0xBA,
    0xC5,0x9B,0x15,0x4D,0x8D,0x2A,0x1E,0x1F,0xEA,0x2B,0x2F,0x64,0xA9,0x50,0x3D,0xAB,

    0x50,0x77,0xE9,0xC0,0xAC,0x6D,0x3F,0xCA,0xCF,0x71,0x7D,0x80,0xA6,0xFD,0xFF,0xB5,
    0xBD,0x6F,0x24,0x7B,0x00,0x99,0x5D,0xB1,0x48,0xB0,0x28,0x7F,0x80,0xEC,0xBF,0x6F,
    0x6E,0x39,0x90,0x42,0xD9,0x4E,0x2E,0x12,0x66,0xC8,0xCF,0x3B,0x3F,0x10,0x7D,0x79,
    0x00,0xD3,0x1F,0x21,0x93,0x34,0xD7,0x19,0x22,0xA2,0x08,0x20,0xB9,0xB9,0xEF,0x51,

    0x99,0xDE,0xBF,0xD4,0x09,0x75,0xE9,0x8A,0xEE,0xFD,0xE4,0x4E,0x30,0x17,0xDF,0xCE,
    0x11,0xB2,0x28,0x35,0xC2,0x7C,0x64,0xEB,0x91,0x5F,0x32,0x0C,0x6E,0x00,0xF9,0x92,
    0x19,0xDB,0x8F,0xAB,0xAE,0xD6,0x12,0xC4,0x26,0x62,0xCE,0xCC,0x0A,0x03,0xE7,0xDD,
    0xE2,0x4D,0x8A,0xA6,0x46,0x95,0x0F,0x8F,0xF5,0x15,0x97,0x32,0xD4,0x28,0x1E,0x55
};

YM2151::YM2151(float volume, uint32_t clock)
{
    this->volume = volume;
    this->clock = clock;
}

YM2151::~YM2151()
{
}

void YM2151::init_tables()
{
    if (tables_initialized)
        return;

    for (int x = 0; x < TL_RES_LEN; x++)
    {
        double m = (1U << 16) / std::pow(2.0, (x + 1) * (ENV_STEP / 4.0) / 8.0);
        m = std::floor(m);

        int n = static_cast<int>(m);
        n >>= 4;
        if (n & 1)
            n = (n >> 1) + 1;
        else
            n = n >> 1;

        n <<= 2;
        tl_tab[x * 2 + 0] = n;
        tl_tab[x * 2 + 1] = -tl_tab[x * 2 + 0];

        for (int i = 1; i < 13; i++)
        {
            tl_tab[x * 2 + 0 + i * 2 * TL_RES_LEN] = tl_tab[x * 2 + 0] >> i;
            tl_tab[x * 2 + 1 + i * 2 * TL_RES_LEN] = -tl_tab[x * 2 + 0 + i * 2 * TL_RES_LEN];
        }
    }

    for (int i = 0; i < SIN_LEN; i++)
    {
        double m = std::sin(((i * 2) + 1) * M_PI / SIN_LEN);
        double o = (m > 0.0)
            ? (8.0 * std::log(1.0 / m) / std::log(2.0))
            : (8.0 * std::log(-1.0 / m) / std::log(2.0));

        o = o / (ENV_STEP / 4.0);

        int n = static_cast<int>(2.0 * o);
        if (n & 1)
            n = (n >> 1) + 1;
        else
            n = n >> 1;

        sin_tab[i] = static_cast<unsigned int>(n * 2 + (m >= 0.0 ? 0 : 1));
    }

    for (int i = 0; i < 16; i++)
    {
        double m = (i != 15 ? i : i + 16) * (4.0 / ENV_STEP);
        d1l_tab[i] = static_cast<uint32_t>(m);
    }

    tables_initialized = true;
}

void YM2151::init_chip_tables()
{
    const double scaler = (static_cast<double>(clock) / 64.0) / static_cast<double>(sampfreq);
    const double mult = static_cast<double>(1U << (FREQ_SH - 10));

    for (int i = 0; i < 768; i++)
    {
        const double phaseinc = static_cast<double>(phaseinc_rom[i]) * scaler;
        freq[768 + 2 * 768 + i] = static_cast<int>(phaseinc * mult) & 0xffffffc0;

        for (int j = 0; j < 2; j++)
        {
            freq[768 + j * 768 + i] = (freq[768 + 2 * 768 + i] >> (2 - j)) & 0xffffffc0;
        }
        for (int j = 3; j < 8; j++)
        {
            freq[768 + j * 768 + i] = freq[768 + 2 * 768 + i] << (j - 2);
        }
    }

    for (int i = 0; i < 768; i++)
    {
        freq[0 * 768 + i] = freq[1 * 768 + 0];
    }

    for (int j = 8; j < 10; j++)
    {
        for (int i = 0; i < 768; i++)
        {
            freq[768 + j * 768 + i] = freq[768 + 8 * 768 - 1];
        }
    }

    const double freq_mult = static_cast<double>(1U << FREQ_SH);
    for (int j = 0; j < 4; j++)
    {
        for (int i = 0; i < 32; i++)
        {
            const double hz = (static_cast<double>(dt1_tab[j * 32 + i]) * (static_cast<double>(clock) / 64.0)) / static_cast<double>(1U << 20);
            const double phaseinc = (hz * SIN_LEN) / static_cast<double>(sampfreq);
            dt1_freq[(j + 0) * 32 + i] = static_cast<int32_t>(phaseinc * freq_mult);
            dt1_freq[(j + 4) * 32 + i] = -dt1_freq[(j + 0) * 32 + i];
        }
    }

    const double timer_mult = static_cast<double>(1U << TIMER_SH);
    for (int i = 0; i < 1024; i++)
    {
        const double pom = (64.0 * (1024.0 - i)) / static_cast<double>(clock);
#ifdef USE_MAME_TIMERS
        timer_A_time[i] = pom;
#else
        tim_A_tab[i] = static_cast<int>(pom * static_cast<double>(sampfreq) * timer_mult);
#endif
    }
    for (int i = 0; i < 256; i++)
    {
        const double pom = (1024.0 * (256.0 - i)) / static_cast<double>(clock);
#ifdef USE_MAME_TIMERS
        timer_B_time[i] = pom;
#else
        tim_B_tab[i] = static_cast<int>(pom * static_cast<double>(sampfreq) * timer_mult);
#endif
    }

    for (int i = 0; i < 32; i++)
    {
        int j = (i != 31 ? i : 30);
        j = 32 - j;
        j = static_cast<int>(65536.0 / static_cast<double>(j * 32.0));
        noise_tab[i] = static_cast<uint32_t>(static_cast<double>(j) * 64.0 * scaler);
    }
}

#define KEY_ON(op, key_set) {                                          \
    if (!(op)->key) {                                                  \
        (op)->phase = 0;                                               \
        (op)->state = EG_ATT;                                          \
        (op)->volume += (~(op)->volume *                               \
            (eg_inc[(op)->eg_sel_ar + ((eg_cnt >> (op)->eg_sh_ar) & 7)]) \
        ) >> 4;                                                        \
        if ((op)->volume <= MIN_ATT_INDEX) {                           \
            (op)->volume = MIN_ATT_INDEX;                              \
            (op)->state = EG_DEC;                                      \
        }                                                              \
    }                                                                  \
    (op)->key |= (key_set);                                            \
}

#define KEY_OFF(op, key_clr) {                                         \
    if ((op)->key) {                                                   \
        (op)->key &= (key_clr);                                        \
        if (!(op)->key) {                                              \
            if ((op)->state > EG_REL)                                  \
                (op)->state = EG_REL;                                  \
        }                                                              \
    }                                                                  \
}

void YM2151::envelope_KONKOFF(YM2151Operator *op, int v)
{
    if (v & 0x08) KEY_ON(op + 0, 1)  else KEY_OFF(op + 0, ~1)
    if (v & 0x20) KEY_ON(op + 1, 1)  else KEY_OFF(op + 1, ~1)
    if (v & 0x10) KEY_ON(op + 2, 1)  else KEY_OFF(op + 2, ~1)
    if (v & 0x40) KEY_ON(op + 3, 1)  else KEY_OFF(op + 3, ~1)
}

#ifdef USE_MAME_TIMERS
static TIMER_CALLBACK(irqAon_callback)
{
    YM2151 *chip = (YM2151 *)ptr;
    int oldstate = irqlinestate;
    irqlinestate |= 1;
    if (oldstate == 0 && irqhandler) (*irqhandler)(device, 1);
}

static TIMER_CALLBACK(irqBon_callback)
{
    YM2151 *chip = (YM2151 *)ptr;
    int oldstate = irqlinestate;
    irqlinestate |= 2;
    if (oldstate == 0 && irqhandler) (*irqhandler)(device, 1);
}

static TIMER_CALLBACK(irqAoff_callback)
{
    YM2151 *chip = (YM2151 *)ptr;
    int oldstate = irqlinestate;
    irqlinestate &= ~1;
    if (oldstate == 1 && irqhandler) (*irqhandler)(device, 0);
}

static TIMER_CALLBACK(irqBoff_callback)
{
    YM2151 *chip = (YM2151 *)ptr;
    int oldstate = irqlinestate;
    irqlinestate &= ~2;
    if (oldstate == 2 && irqhandler) (*irqhandler)(device, 0);
}

static TIMER_CALLBACK(timer_callback_a)
{
    YM2151 *chip = (YM2151 *)ptr;
    timer_A->adjust(timer_A_time[timer_A_index]);
    timer_A_index_old = timer_A_index;
    if (irq_enable & 0x04) {
        status |= 1;
        machine.scheduler().timer_set(attotime::zero, FUNC(irqAon_callback), 0, chip);
    }
    if (irq_enable & 0x80)
        csm_req = 2;
}

static TIMER_CALLBACK(timer_callback_b)
{
    YM2151 *chip = (YM2151 *)ptr;
    timer_B->adjust(timer_B_time[timer_B_index]);
    timer_B_index_old = timer_B_index;
    if (irq_enable & 0x08) {
        status |= 2;
        machine.scheduler().timer_set(attotime::zero, FUNC(irqBon_callback), 0, chip);
    }
}
#endif

void YM2151::set_connect(YM2151Operator *om1, int cha, int v)
{
    YM2151Operator *om2 = om1 + 1;
    YM2151Operator *oc1 = om1 + 2;

    switch (v & 7)
    {
    case 0:
        om1->connects = &c1;
        oc1->connects = &mem;
        om2->connects = &c2;
        om1->mem_connect = &m2;
        break;
    case 1:
        om1->connects = &mem;
        oc1->connects = &mem;
        om2->connects = &c2;
        om1->mem_connect = &m2;
        break;
    case 2:
        om1->connects = &c2;
        oc1->connects = &mem;
        om2->connects = &c2;
        om1->mem_connect = &m2;
        break;
    case 3:
        om1->connects = &c1;
        oc1->connects = &mem;
        om2->connects = &c2;
        om1->mem_connect = &c2;
        break;
    case 4:
        om1->connects = &c1;
        oc1->connects = &chanout[cha];
        om2->connects = &c2;
        om1->mem_connect = &mem;
        break;
    case 5:
        om1->connects = 0;
        oc1->connects = &chanout[cha];
        om2->connects = &chanout[cha];
        om1->mem_connect = &m2;
        break;
    case 6:
        om1->connects = &c1;
        oc1->connects = &chanout[cha];
        om2->connects = &chanout[cha];
        om1->mem_connect = &mem;
        break;
    case 7:
        om1->connects = &chanout[cha];
        oc1->connects = &chanout[cha];
        om2->connects = &chanout[cha];
        om1->mem_connect = &mem;
        break;
    }
}

void YM2151::refresh_EG(YM2151Operator *op)
{
    const uint32_t kc = op->kc;

    for (int k = 0; k < 4; k++, op++)
    {
        const uint32_t v = kc >> op->ks;
        if ((op->ar + v) < 32 + 62)
        {
            op->eg_sh_ar  = eg_rate_shift[op->ar + v];
            op->eg_sel_ar = eg_rate_select[op->ar + v];
        }
        else
        {
            op->eg_sh_ar  = 0;
            op->eg_sel_ar = 17 * RATE_STEPS;
        }
        op->eg_sh_d1r  = eg_rate_shift[op->d1r + v];
        op->eg_sel_d1r = eg_rate_select[op->d1r + v];
        op->eg_sh_d2r  = eg_rate_shift[op->d2r + v];
        op->eg_sel_d2r = eg_rate_select[op->d2r + v];
        op->eg_sh_rr   = eg_rate_shift[op->rr + v];
        op->eg_sel_rr  = eg_rate_select[op->rr + v];
    }
}

void YM2151::write_reg(int r, int v)
{
    YM2151Operator *op = &oper[(r & 0x07) * 4 + ((r & 0x18) >> 3)];

    r &= 0xff;
    v &= 0xff;

    switch (r & 0xe0)
    {
    case 0x00:
        switch (r) {
        case 0x01:
            test = static_cast<uint8_t>(v);
            if (v & 2) lfo_phase = 0;
            break;

        case 0x08:
            envelope_KONKOFF(&oper[(v & 7) * 4], v);
            break;

        case 0x0f:
            noise = static_cast<uint32_t>(v);
            noise_f = noise_tab[v & 0x1f];
            break;

        case 0x10:
            timer_A_index = (timer_A_index & 0x003) | (static_cast<uint32_t>(v) << 2);
            break;

        case 0x11:
            timer_A_index = (timer_A_index & 0x3fc) | (static_cast<uint32_t>(v) & 3);
            break;

        case 0x12:
            timer_B_index = static_cast<uint32_t>(v);
            break;

        case 0x14:
            irq_enable = static_cast<uint32_t>(v);

            if (v & 0x10)
            {
#ifdef USE_MAME_TIMERS
                status &= ~1;
                device->machine().scheduler().timer_set(attotime::zero, FUNC(irqAoff_callback), 0, chip);
#else
                status &= ~1;
#endif
            }

            if (v & 0x20)
            {
#ifdef USE_MAME_TIMERS
                status &= ~2;
                device->machine().scheduler().timer_set(attotime::zero, FUNC(irqBoff_callback), 0, chip);
#else
                status &= ~2;
#endif
            }

            if (v & 0x02)
            {
#ifdef USE_MAME_TIMERS
                if (!timer_B->enable(true)) {
                    timer_B->adjust(timer_B_time[timer_B_index]);
                    timer_B_index_old = timer_B_index;
                }
#else
                if (!tim_B) {
                    tim_B = 1;
                    tim_B_val = static_cast<int32_t>(tim_B_tab[timer_B_index]);
                }
#endif
            }
            else
            {
#ifdef USE_MAME_TIMERS
                timer_B->enable(false);
#else
                tim_B = 0;
#endif
            }

            if (v & 0x01)
            {
#ifdef USE_MAME_TIMERS
                if (!timer_A->enable(true)) {
                    timer_A->adjust(timer_A_time[timer_A_index]);
                    timer_A_index_old = timer_A_index;
                }
#else
                if (!tim_A) {
                    tim_A = 1;
                    tim_A_val = static_cast<int32_t>(tim_A_tab[timer_A_index]);
                }
#endif
            }
            else
            {
#ifdef USE_MAME_TIMERS
                timer_A->enable(false);
#else
                tim_A = 0;
#endif
            }
            break;

        case 0x18:
            lfo_overflow = (1U << ((15 - (v >> 4)) + 3)) * (1U << LFO_SH);
            lfo_counter_add = 0x10U + (static_cast<uint32_t>(v) & 0x0fU);
            break;

        case 0x19:
            if (v & 0x80)
                pmd = static_cast<int8_t>(v & 0x7f);
            else
                amd = static_cast<uint8_t>(v & 0x7f);
            break;

        case 0x1b:
            ct = static_cast<uint8_t>(v >> 6);
            lfo_wsel = static_cast<uint8_t>(v & 3);
            break;

        default:
            break;
        }
        break;

    case 0x20:
        op = &oper[(r & 7) * 4];
        switch (r & 0x18)
        {
        case 0x00:
            op->fb_shift = ((v >> 3) & 7) ? ((v >> 3) & 7) + 6 : 0;
            pan[(r & 7) * 2 + 0] = (v & 0x40) ? ~0U : 0U;
            pan[(r & 7) * 2 + 1] = (v & 0x80) ? ~0U : 0U;
            connects[r & 7] = static_cast<uint8_t>(v & 7);
            set_connect(op, r & 7, v & 7);
            break;

        case 0x08:
            v &= 0x7f;
            if (static_cast<uint32_t>(v) != op->kc)
            {
                uint32_t kc_channel = static_cast<uint32_t>(v - (v >> 2)) * 64U;
                kc_channel += 768U;
                kc_channel |= (op->kc_i & 63U);

                for (int k = 0; k < 4; k++) {
                    (op + k)->kc = static_cast<uint32_t>(v);
                    (op + k)->kc_i = kc_channel;
                }

                const uint32_t kc = static_cast<uint32_t>(v) >> 2;
                for (int k = 0; k < 4; k++) {
                    (op + k)->dt1 = dt1_freq[(op + k)->dt1_i + kc];
                    (op + k)->freq = ((freq[kc_channel + (op + k)->dt2] + (op + k)->dt1) * (op + k)->mul) >> 1;
                }

                refresh_EG(op);
            }
            break;

        case 0x10:
            v >>= 2;
            if (static_cast<uint32_t>(v) != (op->kc_i & 63U))
            {
                const uint32_t kc_channel = static_cast<uint32_t>(v) | (op->kc_i & ~63U);
                for (int k = 0; k < 4; k++) {
                    (op + k)->kc_i = kc_channel;
                    (op + k)->freq = ((freq[kc_channel + (op + k)->dt2] + (op + k)->dt1) * (op + k)->mul) >> 1;
                }
            }
            break;

        case 0x18:
            op->pms = static_cast<uint8_t>((v >> 4) & 7);
            op->ams = static_cast<uint8_t>(v & 3);
            break;
        }
        break;

    case 0x40:
    {
        const uint32_t olddt1_i = op->dt1_i;
        const uint32_t oldmul = op->mul;

        op->dt1_i = static_cast<uint32_t>((v & 0x70) << 1);
        op->mul = (v & 0x0f) ? static_cast<uint32_t>((v & 0x0f) << 1) : 1U;

        if (olddt1_i != op->dt1_i)
            op->dt1 = dt1_freq[op->dt1_i + (op->kc >> 2)];

        if ((olddt1_i != op->dt1_i) || (oldmul != op->mul))
            op->freq = ((freq[op->kc_i + op->dt2] + op->dt1) * op->mul) >> 1;
        break;
    }

    case 0x60:
        op->tl = static_cast<uint32_t>((v & 0x7f) << (ENV_BITS - 7));
        break;

    case 0x80:
    {
        const uint32_t oldks = op->ks;
        const uint32_t oldar = op->ar;

        op->ks = 5U - static_cast<uint32_t>(v >> 6);
        op->ar = (v & 0x1f) ? static_cast<uint32_t>(32 + ((v & 0x1f) << 1)) : 0U;

        if ((op->ar != oldar) || (op->ks != oldks))
        {
            if ((op->ar + (op->kc >> op->ks)) < 32 + 62) {
                op->eg_sh_ar = eg_rate_shift[op->ar + (op->kc >> op->ks)];
                op->eg_sel_ar = eg_rate_select[op->ar + (op->kc >> op->ks)];
            } else {
                op->eg_sh_ar = 0;
                op->eg_sel_ar = 17 * RATE_STEPS;
            }
        }

        if (op->ks != oldks)
        {
            op->eg_sh_d1r = eg_rate_shift[op->d1r + (op->kc >> op->ks)];
            op->eg_sel_d1r = eg_rate_select[op->d1r + (op->kc >> op->ks)];
            op->eg_sh_d2r = eg_rate_shift[op->d2r + (op->kc >> op->ks)];
            op->eg_sel_d2r = eg_rate_select[op->d2r + (op->kc >> op->ks)];
            op->eg_sh_rr = eg_rate_shift[op->rr + (op->kc >> op->ks)];
            op->eg_sel_rr = eg_rate_select[op->rr + (op->kc >> op->ks)];
        }
        break;
    }

    case 0xa0:
        op->AMmask = (v & 0x80) ? ~0U : 0U;
        op->d1r = (v & 0x1f) ? static_cast<uint32_t>(32 + ((v & 0x1f) << 1)) : 0U;
        op->eg_sh_d1r = eg_rate_shift[op->d1r + (op->kc >> op->ks)];
        op->eg_sel_d1r = eg_rate_select[op->d1r + (op->kc >> op->ks)];
        break;

    case 0xc0:
    {
        const uint32_t olddt2 = op->dt2;
        op->dt2 = dt2_tab[v >> 6];
        if (op->dt2 != olddt2)
            op->freq = ((freq[op->kc_i + op->dt2] + op->dt1) * op->mul) >> 1;
        op->d2r = (v & 0x1f) ? static_cast<uint32_t>(32 + ((v & 0x1f) << 1)) : 0U;
        op->eg_sh_d2r = eg_rate_shift[op->d2r + (op->kc >> op->ks)];
        op->eg_sel_d2r = eg_rate_select[op->d2r + (op->kc >> op->ks)];
        break;
    }

    case 0xe0:
        op->d1l = d1l_tab[v >> 4];
        op->rr = static_cast<uint32_t>(34 + ((v & 0x0f) << 2));
        op->eg_sh_rr = eg_rate_shift[op->rr + (op->kc >> op->ks)];
        op->eg_sel_rr = eg_rate_select[op->rr + (op->kc >> op->ks)];
        break;
    }
}

int YM2151::read_status()
{
    return static_cast<int>(status);
}

void YM2151::init(int rate, int fps)
{
    SoundChip::init(STEREO, rate, fps);
    this->sampfreq = rate ? rate : 44100;
    init_tables();
    init_chip_tables();

    lfo_timer_add = static_cast<uint32_t>(static_cast<double>(1U << LFO_SH) * (static_cast<double>(clock) / 64.0) / static_cast<double>(sampfreq));
    eg_timer_add = static_cast<uint32_t>(static_cast<double>(1U << EG_SH) * (static_cast<double>(clock) / 64.0) / static_cast<double>(sampfreq));
    eg_timer_overflow = 3U * (1U << EG_SH);

#ifdef USE_MAME_TIMERS
    PSG->timer_A = device->machine().scheduler().timer_alloc(FUNC(timer_callback_a), PSG);
    PSG->timer_B = device->machine().scheduler().timer_alloc(FUNC(timer_callback_b), PSG);
#else
    tim_A = 0;
    tim_B = 0;
#endif
    ym2151_reset_chip();
}

void ym2151_shutdown()
{
}

void YM2151::ym2151_reset_chip()
{
    for (int i = 0; i < 32; i++)
    {
        std::memset(&oper[i], 0, sizeof(YM2151Operator));
        oper[i].volume = MAX_ATT_INDEX;
        oper[i].kc_i = 768;
    }

    std::memset(chanout, 0, sizeof(chanout));

    eg_timer = 0;
    eg_cnt = 0;

    lfo_timer = 0;
    lfo_counter = 0;
    lfo_phase = 0;
    lfo_wsel = 0;
    pmd = 0;
    amd = 0;
    lfa = 0;
    lfp = 0;
    test = 0;
    irq_enable = 0;

#ifdef USE_MAME_TIMERS
    timer_A->enable(false);
    timer_B->enable(false);
#else
    tim_A = 0;
    tim_B = 0;
    tim_A_val = 0;
    tim_B_val = 0;
#endif
    timer_A_index = 0;
    timer_B_index = 0;
    timer_A_index_old = 0;
    timer_B_index_old = 0;

    noise = 0;
    noise_rng = 0;
    noise_p = 0;
    noise_f = noise_tab[0];

    csm_req = 0;
    status = 0;

    write_reg(0x1b, 0);
    write_reg(0x18, 0);
    for (int i = 0x20; i < 0x100; i++)
    {
        write_reg(i, 0);
    }
}

signed int YM2151::op_calc(YM2151Operator *OP, unsigned int env, signed int pm)
{
    const uint32_t p = (env << 3) + sin_tab[(static_cast<signed int>((OP->phase & ~FREQ_MASK) + (pm << 15)) >> FREQ_SH) & SIN_MASK];
    if (p >= TL_TAB_LEN)
        return 0;
    return tl_tab[p];
}

signed int YM2151::op_calc1(YM2151Operator *OP, unsigned int env, signed int pm)
{
    const int32_t i = static_cast<int32_t>(OP->phase & ~FREQ_MASK) + pm;
    const uint32_t p = (env << 3) + sin_tab[(i >> FREQ_SH) & SIN_MASK];
    if (p >= TL_TAB_LEN)
        return 0;
    return tl_tab[p];
}

#define volume_calc(OP) ((OP)->tl + static_cast<uint32_t>((OP)->volume) + (AM & (OP)->AMmask))

void YM2151::chan_calc(unsigned int chan)
{
    m2 = c1 = c2 = mem = 0;
    YM2151Operator *op = &oper[chan * 4];
    uint32_t AM = 0;

    *op->mem_connect = op->mem_value;

    if (op->ams)
        AM = lfa << (op->ams - 1);

    unsigned int env = volume_calc(op);
    {
        const int32_t out = op->fb_out_prev + op->fb_out_curr;
        op->fb_out_prev = op->fb_out_curr;

        if (!op->connects)
            mem = c1 = c2 = op->fb_out_prev;
        else
            *op->connects = op->fb_out_prev;

        op->fb_out_curr = 0;
        if (env < ENV_QUIET)
        {
            const int32_t fb_in = op->fb_shift ? (out << op->fb_shift) : 0;
            op->fb_out_curr = op_calc1(op, env, fb_in);
        }
    }

    env = volume_calc(op + 1);
    if (env < ENV_QUIET)
        *(op + 1)->connects += op_calc(op + 1, env, m2);

    env = volume_calc(op + 2);
    if (env < ENV_QUIET)
        *(op + 2)->connects += op_calc(op + 2, env, c1);

    env = volume_calc(op + 3);
    if (env < ENV_QUIET)
        chanout[chan] += op_calc(op + 3, env, c2);

    op->mem_value = mem;
}

void YM2151::chan7_calc()
{
    m2 = c1 = c2 = mem = 0;
    YM2151Operator *op = &oper[7 * 4];
    uint32_t AM = 0;

    *op->mem_connect = op->mem_value;

    if (op->ams)
        AM = lfa << (op->ams - 1);

    unsigned int env = volume_calc(op);
    {
        const int32_t out = op->fb_out_prev + op->fb_out_curr;
        op->fb_out_prev = op->fb_out_curr;

        if (!op->connects)
            mem = c1 = c2 = op->fb_out_prev;
        else
            *op->connects = op->fb_out_prev;

        op->fb_out_curr = 0;
        if (env < ENV_QUIET)
        {
            const int32_t fb_in = op->fb_shift ? (out << op->fb_shift) : 0;
            op->fb_out_curr = op_calc1(op, env, fb_in);
        }
    }

    env = volume_calc(op + 1);
    if (env < ENV_QUIET)
        *(op + 1)->connects += op_calc(op + 1, env, m2);

    env = volume_calc(op + 2);
    if (env < ENV_QUIET)
        *(op + 2)->connects += op_calc(op + 2, env, c1);

    env = volume_calc(op + 3);
    if (noise & 0x80)
    {
        int32_t noiseout = 0;
        if (env < 0x3ff)
            noiseout = static_cast<int32_t>(env ^ 0x3ff) * 2;
        chanout[7] += (noise_rng & 0x10000) ? noiseout : -noiseout;
    }
    else
    {
        if (env < ENV_QUIET)
            chanout[7] += op_calc(op + 3, env, c2);
    }

    op->mem_value = mem;
}

void YM2151::advance_eg()
{
    eg_timer += eg_timer_add;

    while (eg_timer >= eg_timer_overflow)
    {
        eg_timer -= eg_timer_overflow;
        eg_cnt++;

        YM2151Operator *op = &oper[0];
        for (unsigned int i = 0; i < 32; i++, op++)
        {
            switch (op->state)
            {
            case EG_ATT:
                if (!(eg_cnt & ((1U << op->eg_sh_ar) - 1U)))
                {
                    op->volume += (~op->volume * eg_inc[op->eg_sel_ar + ((eg_cnt >> op->eg_sh_ar) & 7)]) >> 4;
                    if (op->volume <= MIN_ATT_INDEX) {
                        op->volume = MIN_ATT_INDEX;
                        op->state = EG_DEC;
                    }
                }
                break;

            case EG_DEC:
                if (!(eg_cnt & ((1U << op->eg_sh_d1r) - 1U)))
                {
                    op->volume += eg_inc[op->eg_sel_d1r + ((eg_cnt >> op->eg_sh_d1r) & 7)];
                    if (op->volume >= static_cast<int32_t>(op->d1l))
                        op->state = EG_SUS;
                }
                break;

            case EG_SUS:
                if (!(eg_cnt & ((1U << op->eg_sh_d2r) - 1U)))
                {
                    op->volume += eg_inc[op->eg_sel_d2r + ((eg_cnt >> op->eg_sh_d2r) & 7)];
                    if (op->volume >= MAX_ATT_INDEX) {
                        op->volume = MAX_ATT_INDEX;
                        op->state = EG_OFF;
                    }
                }
                break;

            case EG_REL:
                if (!(eg_cnt & ((1U << op->eg_sh_rr) - 1U)))
                {
                    op->volume += eg_inc[op->eg_sel_rr + ((eg_cnt >> op->eg_sh_rr) & 7)];
                    if (op->volume >= MAX_ATT_INDEX) {
                        op->volume = MAX_ATT_INDEX;
                        op->state = EG_OFF;
                    }
                }
                break;
            }
        }
    }
}

void YM2151::advance()
{
    int a = 0, p = 0;

    if (test & 2)
        lfo_phase = 0;
    else
    {
        lfo_timer += lfo_timer_add;
        if (lfo_timer >= lfo_overflow)
        {
            lfo_timer -= lfo_overflow;
            lfo_counter += lfo_counter_add;
            lfo_phase += (lfo_counter >> 4);
            lfo_phase &= 255;
            lfo_counter &= 15;
        }
    }

    const uint32_t idx = lfo_phase;
    switch (lfo_wsel)
    {
    case 0:
        a = static_cast<int>(255U - idx);
        p = (idx < 128U) ? static_cast<int>(idx) : static_cast<int>(idx) - 255;
        break;
    case 1:
        if (idx < 128U) {
            a = 255;
            p = 128;
        } else {
            a = 0;
            p = -128;
        }
        break;
    case 2:
        a = (idx < 128U) ? static_cast<int>(255U - (idx * 2U)) : static_cast<int>((idx * 2U) - 256U);
        if (idx < 64U)
            p = static_cast<int>(idx * 2U);
        else if (idx < 128U)
            p = static_cast<int>(255U - idx * 2U);
        else if (idx < 192U)
            p = static_cast<int>(256U - idx * 2U);
        else
            p = static_cast<int>(idx * 2U - 511U);
        break;
    case 3:
    default:
        a = lfo_noise_waveform[idx];
        p = a - 128;
        break;
    }
    lfa = static_cast<uint32_t>(a * amd / 128);
    lfp = p * pmd / 128;

    noise_p += noise_f;
    uint32_t shifts = (noise_p >> 16);
    noise_p &= 0xffff;
    while (shifts--)
    {
        const uint32_t j = ((noise_rng ^ (noise_rng >> 3)) & 1U) ^ 1U;
        noise_rng = (j << 16) | (noise_rng >> 1);
    }

    YM2151Operator *op = &oper[0];
    for (unsigned int ch = 0; ch < 8; ch++, op += 4)
    {
        if (op->pms)
        {
            int32_t mod_ind = lfp;
            if (op->pms < 6)
                mod_ind >>= (6 - op->pms);
            else
                mod_ind <<= (op->pms - 5);

            if (mod_ind)
            {
                const uint32_t kc_channel = static_cast<uint32_t>(static_cast<int32_t>(op->kc_i) + mod_ind);
                for (int k = 0; k < 4; k++) {
                    (op + k)->phase += ((freq[kc_channel + (op + k)->dt2] + (op + k)->dt1) * (op + k)->mul) >> 1;
                }
            }
            else
            {
                for (int k = 0; k < 4; k++) {
                    (op + k)->phase += (op + k)->freq;
                }
            }
        }
        else
        {
            for (int k = 0; k < 4; k++) {
                (op + k)->phase += (op + k)->freq;
            }
        }
    }

    if (csm_req)
    {
        if (csm_req == 2)
        {
            op = &oper[0];
            for (unsigned int i = 0; i < 32; i++, op++) {
                KEY_ON(op, 2)
            }
            csm_req = 1;
        }
        else
        {
            op = &oper[0];
            for (unsigned int i = 0; i < 32; i++, op++) {
                KEY_OFF(op, ~2)
            }
            csm_req = 0;
        }
    }
}

void YM2151::stream_update()
{
    SoundChip::clear_buffer();
    const uint32_t length = frame_size;

#ifndef USE_MAME_TIMERS
    if (tim_B)
    {
        tim_B_val -= static_cast<int32_t>(length << TIMER_SH);
        if (tim_B_val <= 0)
        {
            tim_B_val += static_cast<int32_t>(tim_B_tab[timer_B_index]);
            if (irq_enable & 0x08)
            {
                const uint32_t oldstate = status & 3U;
                status |= 2U;
                if (oldstate == 0) irq = true;
            }
        }
    }
#endif

    for (uint32_t i = 0; i < length; i++)
    {
        advance_eg();

        std::memset(chanout, 0, sizeof(chanout));

        for (unsigned int ch = 0; ch < 7; ch++) {
            chan_calc(ch);
        }
        chan7_calc();

        int32_t outl = 0;
        int32_t outr = 0;
        for (unsigned int ch = 0; ch < 8; ch++) {
            outl += static_cast<int32_t>(static_cast<uint32_t>(chanout[ch]) & pan[ch * 2 + 0]);
            outr += static_cast<int32_t>(static_cast<uint32_t>(chanout[ch]) & pan[ch * 2 + 1]);
        }

        outl >>= FINAL_SH;
        outr >>= FINAL_SH;
        if (outl > MAXOUT) outl = MAXOUT;
        else if (outl < MINOUT) outl = MINOUT;
        if (outr > MAXOUT) outr = MAXOUT;
        else if (outr < MINOUT) outr = MINOUT;

        write_buffer(LEFT, i, static_cast<int16_t>(static_cast<float>(outl) * volume));
        write_buffer(RIGHT, i, static_cast<int16_t>(static_cast<float>(outr) * volume));

#ifndef USE_MAME_TIMERS
        if (tim_A)
        {
            tim_A_val -= (1 << TIMER_SH);
            if (tim_A_val <= 0)
            {
                tim_A_val += static_cast<int32_t>(tim_A_tab[timer_A_index]);
                if (irq_enable & 0x04)
                {
                    const uint32_t oldstate = status & 3U;
                    status |= 1U;
                    if (oldstate == 0) irq = true;
                }
                if (irq_enable & 0x80)
                    csm_req = 2;
            }
        }
#endif
        advance();
    }
}
