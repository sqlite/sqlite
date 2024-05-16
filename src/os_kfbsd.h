#ifndef FREEBSD_KERNEL_H
#define FREEBSD_KERNEL_H

//
// Note that this code is copy-pasted from several sources:
//
// https://opensource.apple.com/source/clang/clang-211.10.1/src/projects/compiler-rt/lib/floatdidf.c.auto.html
// https://opensource.apple.com/source/clang/clang-700.0.72/src/projects/compiler-rt/lib/builtins/comparedf2.c.auto.html
// https://opensource.apple.com/source/clang/clang-700.1.81/src/projects/compiler-rt/lib/builtins/fp_lib.h.auto.html
//
// 

typedef uint32_t rep_t;
typedef int32_t srep_t;

#define REP_C UINT32_C
#define significandBits 23

typedef double fp_t;

#define CHAR_BIT        8

#define typeWidth       (sizeof(rep_t)*CHAR_BIT)
#define exponentBits    (typeWidth - significandBits - 1)
#define maxExponent     ((1 << exponentBits) - 1)
#define exponentBias    (maxExponent >> 1)

#define implicitBit     (REP_C(1) << significandBits)
#define significandMask (implicitBit - 1U)
#define signBit         (REP_C(1) << (significandBits + exponentBits))
#define absMask         (signBit - 1U)
#define exponentMask    (absMask ^ significandMask)
#define oneRep          ((rep_t)exponentBias << significandBits)
#define infRep          exponentMask
#define quietBit        (implicitBit >> 1)
#define qnanRep         (exponentMask | quietBit)

static inline rep_t toRep(fp_t x) {
    const union { fp_t f; rep_t i; } rep = {.f = x};
    return rep.i;
}

enum LE_RESULT {
    LE_LESS      = -1,
    LE_EQUAL     =  0,
    LE_GREATER   =  1,
    LE_UNORDERED =  1
};

enum GE_RESULT {
    GE_LESS      = -1,
    GE_EQUAL     =  0,
    GE_GREATER   =  1,
    GE_UNORDERED = -1   // Note: different from LE_UNORDERED
};


double __floatdidf(long long a) {
    
    static const double twop52 = 0x1.0p52;
    static const double twop32 = 0x1.0p32;
    
    union { int64_t x; double d; } low = { .d = twop52 };
    
    const double high = (int32_t)(a >> 32) * twop32;
    low.x |= a & INT64_C(0x00000000ffffffff);
    
    const double result = (high - twop52) + low.d;
    return result;    
}

double __floatditf(long a) {
    //todo: STELIOS
    printf("Warning: Symbol __floatditf is undefined - this function should not be called!\n");
    return 0.0;
}

int __gtdf2(double a, double b) {

    const srep_t aInt = toRep(a);
    const srep_t bInt = toRep(b);
    const rep_t aAbs = aInt & absMask;
    const rep_t bAbs = bInt & absMask;
    
    if (aAbs > infRep || bAbs > infRep) return GE_UNORDERED;
    if ((aAbs | bAbs) == 0) return GE_EQUAL;
    if ((aInt & bInt) >= 0) {
        if (aInt < bInt) return GE_LESS;
        else if (aInt == bInt) return GE_EQUAL;
        else return GE_GREATER;
    } else {
        if (aInt > bInt) return GE_LESS;
        else if (aInt == bInt) return GE_EQUAL;
        else return GE_GREATER;
    }
    
}

int __ltdf2(double a, double b) {
        
    const srep_t aInt = toRep(a);
    const srep_t bInt = toRep(b);
    const rep_t aAbs = aInt & absMask;
    const rep_t bAbs = bInt & absMask;
    
    // If either a or b is NaN, they are unordered.
    if (aAbs > infRep || bAbs > infRep) return LE_UNORDERED;
    
    // If a and b are both zeros, they are equal.
    if ((aAbs | bAbs) == 0) return LE_EQUAL;
    
    // If at least one of a and b is positive, we get the same result comparing
    // a and b as signed integers as we would with a floating-point compare.
    if ((aInt & bInt) >= 0) {
        if (aInt < bInt) return LE_LESS;
        else if (aInt == bInt) return LE_EQUAL;
        else return LE_GREATER;
    }
    
    // Otherwise, both are negative, so we need to flip the sense of the
    // comparison to get the correct result.  (This assumes a twos- or ones-
    // complement integer representation; if integers are represented in a
    // sign-magnitude representation, then this flip is incorrect).
    else {
        if (aInt > bInt) return LE_LESS;
        else if (aInt == bInt) return LE_EQUAL;
        else return LE_GREATER;
    }
}

int __gedf2(double a, double b) {
    //todo: STELIOS
    printf("Warning: Symbol __gedf2 is undefined - this function should not be called!\n");
    return 0;
}

double __multf3(double a, double b) {
    //todo: STELIOS
    printf("Warning: Symbol __multf3 is undefined - this function should not be called!\n");
    return 0.0;
}

double __muldf3(double a, double b) {
    //todo: STELIOS
    printf("Warning: Symbol __muldf3 is undefined - this function should not be called!\n");
    return 0.0;
}

int __fixdfdi(double a) {
    //todo: STELIOS
    printf("Warning: Symbol __fixdfdi is undefined - this function should not be called!\n");
    return 0;
}

int __fixtfdi(double a) {
    //todo: STELIOS
    printf("Warning: Symbol __fixtfdi is undefined - this function should not be called!\n");
    return 0;
}

int __fixdfsi(double a) {
    //todo: STELIOS
    printf("Warning: Symbol __fixdfsi is undefined - this function should not be called!\n");
    return 0;
}

double fma(double a, double b, double c) {
    //todo: STELIOS
    printf("Warning: Symbol fma is undefined - this function should not be called!\n");
    return 0.0;
}

#endif
