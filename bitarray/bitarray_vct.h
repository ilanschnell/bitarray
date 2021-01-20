#if (defined(__GNUC__) || defined(__clang__))
#define HAS_VECTORS 1
#else
#define HAS_VECTORS 0
#endif

#if HAS_VECTORS
typedef char vec __attribute__((vector_size(16)));
#endif

#if (defined(__GNUC__) || defined(__clang__))
#define ATTR_UNUSED __attribute__((__unused__))
#else
#define ATTR_UNUSED
#endif

#define UNUSEDVAR(x) (void)x

#if HAS_VECTORS
/*
  * Perform bitwise operation OP on 16 bytes of memory at a time.
  */
 #define vector_op(A, B, OP) do {  \
     vec __a, __b, __r;            \
     memcpy(&__a, A, sizeof(vec)); \
     memcpy(&__b, B, sizeof(vec)); \
     __r = __a OP __b;             \
     memcpy(A, &__r, sizeof(vec)); \
 } while(0);

static const vec bitv_00  = {0x00, 0x00, 0x00, 0x00, (char)0x00, (char)0x00, (char)0x00, (char)0x00, (char)0x00, (char)0x00, (char)0x00, (char)0x00, (char)0x00, (char)0x00, (char)0x00, (char)0x00};
static const vec bitv_ff  = {(char)0xff, (char)0xff, (char)0xff, (char)0xff, (char)0xff, (char)0xff, (char)0xff, (char)0xff, (char)0xff, (char)0xff, (char)0xff, (char)0xff, (char)0xff, (char)0xff, (char)0xff, (char)0xff};

#endif

//types and constants used in the functions below
//uint64_t is an unsigned 64-bit integer variable type (defined in C99 version of C language)
static const uint64_t bit_m1  = 0x5555555555555555; //binary: 0101...
static const uint64_t bit_m2  = 0x3333333333333333; //binary: 00110011..
static const uint64_t bit_m4  = 0x0f0f0f0f0f0f0f0f; //binary:  4 zeros,  4 ones ...
static const uint64_t bit_h01 = 0x0101010101010101; //the sum of 256 to the power of 0,1,2,3...

// http://bisqwit.iki.fi/source/misc/bitcounting/#Wp3NiftyRevised
#define BITWISE_HW_WP3(tmpx, hw) do {                    \
  tmpx -= (tmpx >> 1) & bit_m1;                       \
  tmpx = (tmpx & bit_m2) + ((tmpx >> 2) & bit_m2);    \
  tmpx = (tmpx + (tmpx >> 4)) & bit_m4;               \
  hw += (tmpx * bit_h01) >> 56;                       \
  } while(0)

