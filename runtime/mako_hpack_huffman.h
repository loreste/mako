/* RFC 7541 Appendix B Huffman — generated. Do not edit by hand. */
#ifndef MAKO_HPACK_HUFFMAN_H
#define MAKO_HPACK_HUFFMAN_H

#include "mako_rt.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const uint32_t mako_huff_code[256] = {8184,8388568,268435426,268435427,268435428,268435429,268435430,268435431,268435432,16777194,1073741820,268435433,268435434,1073741821,268435435,268435436,268435437,268435438,268435439,268435440,268435441,268435442,1073741822,268435443,268435444,268435445,268435446,268435447,268435448,268435449,268435450,268435451,20,1016,1017,4090,8185,21,248,2042,1018,1019,249,2043,250,22,23,24,0,1,2,25,26,27,28,29,30,31,92,251,32764,32,4091,1020,8186,33,93,94,95,96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,252,115,253,8187,524272,8188,16380,34,32765,3,35,4,36,5,37,38,39,6,116,117,40,41,42,7,43,118,44,8,9,45,119,120,121,122,123,32766,2044,16381,8189,268435452,1048550,4194258,1048551,1048552,4194259,4194260,4194261,8388569,4194262,8388570,8388571,8388572,8388573,8388574,16777195,8388575,16777196,16777197,4194263,8388576,16777198,8388577,8388578,8388579,8388580,2097116,4194264,8388581,4194265,8388582,8388583,16777199,4194266,2097117,1048553,4194267,4194268,8388584,8388585,2097118,8388586,4194269,4194270,16777200,2097119,4194271,8388587,8388588,2097120,2097121,4194272,2097122,8388589,4194273,8388590,8388591,1048554,4194274,4194275,4194276,8388592,4194277,4194278,8388593,67108832,67108833,1048555,524273,4194279,8388594,4194280,33554412,67108834,67108835,67108836,134217694,134217695,67108837,16777201,33554413,524274,2097123,67108838,134217696,134217697,67108839,134217698,16777202,2097124,2097125,67108840,67108841,268435453,134217699,134217700,134217701,1048556,16777203,1048557,2097126,4194281,2097127,2097128,8388595,4194282,4194283,33554414,33554415,16777204,16777205,67108842,8388596,67108843,134217702,67108844,67108845,134217703,134217704,134217705,134217706,134217707,268435454,134217708,134217709,134217710,134217711,134217712,67108846};
static const uint8_t mako_huff_bits[256] = {13,23,28,28,28,28,28,28,28,24,30,28,28,30,28,28,28,28,28,28,28,28,30,28,28,28,28,28,28,28,28,28,6,10,10,12,13,6,8,11,10,10,8,11,8,6,6,6,5,5,5,6,6,6,6,6,6,6,7,8,15,6,12,10,13,6,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,8,7,8,13,19,13,14,6,15,5,6,5,6,5,6,6,6,5,7,7,6,6,6,5,6,7,6,5,5,6,7,7,7,7,7,15,11,14,13,28,20,22,20,20,22,22,22,23,22,23,23,23,23,23,24,23,24,24,22,23,24,23,23,23,23,21,22,23,22,23,23,24,22,21,20,22,22,23,23,21,23,22,22,24,21,22,23,23,21,21,22,21,23,22,23,23,20,22,22,22,23,22,22,23,26,26,20,19,22,23,22,25,26,26,26,27,27,26,24,25,19,21,26,27,27,26,27,24,21,21,26,26,28,27,27,27,20,24,20,21,22,21,21,23,22,22,25,25,24,24,26,23,26,27,26,26,27,27,27,27,27,28,27,27,27,27,27,26};

/* Encode ASCII/bytes with HPACK Huffman. Max input 256 bytes. */
static inline MakoString mako_hpack_huffman_encode(MakoString s) {
    size_t n = s.data ? s.len : 0;
    if (n > 256) return (MakoString){NULL, 0};
    /* worst case ~30 bits/byte → ~4 bytes out per in + 1 */
    size_t cap = n * 4 + 8;
    unsigned char *out = (unsigned char *)malloc(cap);
    if (!out) return (MakoString){NULL, 0};
    uint64_t bits = 0;
    int nbits = 0;
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char ch = (unsigned char)s.data[i];
        uint32_t code = mako_huff_code[ch];
        int blen = (int)mako_huff_bits[ch];
        bits = (bits << blen) | code;
        nbits += blen;
        while (nbits >= 8) {
            nbits -= 8;
            if (o >= cap) { free(out); return (MakoString){NULL, 0}; }
            out[o++] = (unsigned char)((bits >> nbits) & 0xff);
            bits &= (nbits ? (((uint64_t)1 << nbits) - 1) : 0);
        }
    }
    if (nbits > 0) {
        int pad = 8 - nbits;
        bits = (bits << pad) | (((uint64_t)1 << pad) - 1); /* EOS prefix = ones */
        if (o >= cap) { free(out); return (MakoString){NULL, 0}; }
        out[o++] = (unsigned char)(bits & 0xff);
    }
    char *d = (char *)malloc(o + 1);
    if (!d) { free(out); return (MakoString){NULL, 0}; }
    memcpy(d, out, o);
    d[o] = 0;
    free(out);
    return (MakoString){d, o};
}

/* Decode HPACK Huffman bytes → string. Empty on failure. Max out 512. */
static inline MakoString mako_hpack_huffman_decode(MakoString enc) {
    if (!enc.data || enc.len == 0) return (MakoString){NULL, 0};
    char *out = (char *)malloc(513);
    if (!out) return (MakoString){NULL, 0};
    size_t o = 0;
    uint64_t bits = 0;
    int nbits = 0;
    size_t i = 0;
    const unsigned char *p = (const unsigned char *)enc.data;
    size_t n = enc.len;
    for (;;) {
        /* need more input bits if we cannot match yet */
        int matched = 0;
        for (int sym = 0; sym < 256; sym++) {
            int blen = (int)mako_huff_bits[sym];
            if (nbits < blen) continue;
            uint32_t code = mako_huff_code[sym];
            uint64_t top = (bits >> (nbits - blen)) & ((((uint64_t)1 << blen) - 1));
            if (top == code) {
                if (o >= 512) { free(out); return (MakoString){NULL, 0}; }
                out[o++] = (char)sym;
                nbits -= blen;
                bits &= (nbits ? (((uint64_t)1 << nbits) - 1) : 0);
                matched = 1;
                break;
            }
        }
        if (matched) continue;
        if (i >= n) break;
        bits = (bits << 8) | p[i++];
        nbits += 8;
        if (nbits > 56) { /* safety */
            free(out);
            return (MakoString){NULL, 0};
        }
    }
    /* remaining bits should be EOS padding (all 1s), ignore */
    out[o] = 0;
    return (MakoString){out, o};
}

#endif /* MAKO_HPACK_HUFFMAN_H */
