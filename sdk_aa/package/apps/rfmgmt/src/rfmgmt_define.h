#ifndef CS_DEFINE_H_
#define CS_DEFINE_H_

typedef unsigned char       b8;
typedef unsigned char       u8;
typedef unsigned short      u16;
typedef unsigned int        u32;
typedef signed char         s8;
typedef signed short        s16;
typedef signed int          s32;
typedef float               f32;
typedef double              f64;
typedef unsigned long long  u64;

#undef ABS
#define ABS(x)      (((x) < 0) ? (-(x)) : (x))

#undef MAX
#define MAX(a, b)   (((a) > (b)) ? (a) : (b))

#undef MIN
#define MIN(a, b)   (((a) > (b)) ? (b) : (a))

#define NUM_ELEMS(x) (sizeof(x)/sizeof(x[0]))

#ifndef TRUE
enum {
  FALSE = 0,
  TRUE,
};
#endif

#undef NULL
#if defined(__cplusplus)
#define NULL 0
#else
#define NULL ((void *)0)
#endif


#endif /* CS_DEFINE_H_ */