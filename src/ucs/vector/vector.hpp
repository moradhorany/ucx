#include <mpi.h>
#include <stdint.h>
#include <stdbool.h>

#include <ucs/vector/mipp/mipp.h>

#ifdef OMPI_MPI_H
/* OpenMPI-specific definitions */
#define OPAL_OBJECT_H
#define restrict
struct _a {};
struct opal_object_t {};
typedef unsigned long int opal_atomic_intptr_t;
#define OBJ_CLASS_DECLARATION(_name) extern int dummy ## __LINE__
#define OPAL_UNLIKELY(x) (x)
#include "ompi/op/op.h"
#define UCS_VECTOR_MPI_GET_OP_ID(_op)     (((struct ompi_op_t*)_op)->op_type)
#define UCS_VECTOR_MPI_GET_DTYPE_ID(_dt)  (((struct ompi_datatype_t*)_dt)->id)

#define UCS_VECTOR_MPI_BYTE               ( 1)
#define UCS_VECTOR_MPI_PACKED             ( 2)
#define UCS_VECTOR_MPI_UB                 ( 3)
#define UCS_VECTOR_MPI_LB                 ( 4)
#define UCS_VECTOR_MPI_CHARACTER          ( 5)
#define UCS_VECTOR_MPI_LOGICAL            ( 6)
#define UCS_VECTOR_MPI_INTEGER            ( 7)
#define UCS_VECTOR_MPI_INTEGER1           ( 8)
#define UCS_VECTOR_MPI_INTEGER2           ( 9)
#define UCS_VECTOR_MPI_INTEGER4           (10)
#define UCS_VECTOR_MPI_INTEGER8           (11)
#define UCS_VECTOR_MPI_INTEGER16          (12)
#define UCS_VECTOR_MPI_REAL               (13)
#define UCS_VECTOR_MPI_REAL4              (14)
#define UCS_VECTOR_MPI_REAL8              (15)
#define UCS_VECTOR_MPI_REAL16             (16)
#define UCS_VECTOR_MPI_DOUBLE_PRECISION   (17)
#define UCS_VECTOR_MPI_COMPLEX            (18)
#define UCS_VECTOR_MPI_COMPLEX8           (19)
#define UCS_VECTOR_MPI_COMPLEX16          (20)
#define UCS_VECTOR_MPI_COMPLEX32          (21)
#define UCS_VECTOR_MPI_DOUBLE_COMPLEX     (22)
#define UCS_VECTOR_MPI_2REAL              (23)
#define UCS_VECTOR_MPI_2DOUBLE_PRECISION  (24)
#define UCS_VECTOR_MPI_2INTEGER           (25)
#define UCS_VECTOR_MPI_2COMPLEX           (26)
#define UCS_VECTOR_MPI_2DOUBLE_COMPLEX    (27)
#define UCS_VECTOR_MPI_REAL2              (28)
#define UCS_VECTOR_MPI_LOGICAL1           (29)
#define UCS_VECTOR_MPI_LOGICAL2           (30)
#define UCS_VECTOR_MPI_LOGICAL4           (31)
#define UCS_VECTOR_MPI_LOGICAL8           (32)
#define UCS_VECTOR_MPI_WCHAR              (33)
#define UCS_VECTOR_MPI_CHAR               (34)
#define UCS_VECTOR_MPI_UNSIGNED_CHAR      (35)
#define UCS_VECTOR_MPI_SIGNED_CHAR        (36)
#define UCS_VECTOR_MPI_SHORT              (37)
#define UCS_VECTOR_MPI_UNSIGNED_SHORT     (38)
#define UCS_VECTOR_MPI_INT                (39)
#define UCS_VECTOR_MPI_UNSIGNED           (40)
#define UCS_VECTOR_MPI_LONG               (41)
#define UCS_VECTOR_MPI_UNSIGNED_LONG      (42)
#define UCS_VECTOR_MPI_LONG_LONG_INT      (43)
#define UCS_VECTOR_MPI_UNSIGNED_LONG_LONG (44)
#define UCS_VECTOR_MPI_FLOAT              (45)
#define UCS_VECTOR_MPI_DOUBLE             (46)
#define UCS_VECTOR_MPI_LONG_DOUBLE        (47)
#define UCS_VECTOR_MPI_FLOAT_INT          (48)
#define UCS_VECTOR_MPI_DOUBLE_INT         (49)
#define UCS_VECTOR_MPI_LONGDBL_INT        (50)
#define UCS_VECTOR_MPI_LONG_INT           (51)
#define UCS_VECTOR_MPI_2INT               (52)
#define UCS_VECTOR_MPI_SHORT_INT          (53)
#define UCS_VECTOR_MPI_CXX_BOOL           (54)
#define UCS_VECTOR_MPI_CXX_CPLEX          (55)
#define UCS_VECTOR_MPI_CXX_DBLCPLEX       (56)
#define UCS_VECTOR_MPI_CXX_LDBLCPLEX      (57)
#define UCS_VECTOR_MPI_INT8_T             (58)
#define UCS_VECTOR_MPI_UINT8_T            (59)
#define UCS_VECTOR_MPI_INT16_T            (60)
#define UCS_VECTOR_MPI_UINT16_T           (61)
#define UCS_VECTOR_MPI_INT32_T            (62)
#define UCS_VECTOR_MPI_UINT32_T           (63)
#define UCS_VECTOR_MPI_INT64_T            (64)
#define UCS_VECTOR_MPI_UINT64_T           (65)
#define UCS_VECTOR_MPI_MAX_DTYPE          (66)

#define UCS_VECTOR_MPI_MAX                ( 1)
#define UCS_VECTOR_MPI_MIN                ( 2)
#define UCS_VECTOR_MPI_SUM                ( 3)
#define UCS_VECTOR_MPI_PROD               ( 4)
#define UCS_VECTOR_MPI_LAND               ( 5)
#define UCS_VECTOR_MPI_BAND               ( 6)
#define UCS_VECTOR_MPI_LOR                ( 7)
#define UCS_VECTOR_MPI_BOR                ( 8)
#define UCS_VECTOR_MPI_LXOR               ( 9)
#define UCS_VECTOR_MPI_BXOR               (10)
#define UCS_VECTOR_MPI_MAXLOC             (11)
#define UCS_VECTOR_MPI_MINLOC             (12)
#define UCS_VECTOR_MPI_MAX_OP             (13)

#else
/* Default definitions for MPI */
#define UCS_VECTOR_MPI_GET_OP_ID(_op)     ((uintptr)_op)
#define UCS_VECTOR_MPI_GET_DTYPE_ID(_dt)  ((uintptr)_dt)

#define UCS_VECTOR_MPI_BYTE               MPI_BYTE
#define UCS_VECTOR_MPI_PACKED             MPI_PACKED
#define UCS_VECTOR_MPI_UB                 MPI_UB
#define UCS_VECTOR_MPI_LB                 MPI_LB
#define UCS_VECTOR_MPI_CHARACTER          MPI_CHARACTER
#define UCS_VECTOR_MPI_LOGICAL            MPI_LOGICAL
#define UCS_VECTOR_MPI_INTEGER            MPI_INTEGER
#define UCS_VECTOR_MPI_INTEGER1           MPI_INTEGER1
#define UCS_VECTOR_MPI_INTEGER2           MPI_INTEGER2
#define UCS_VECTOR_MPI_INTEGER4           MPI_INTEGER4
#define UCS_VECTOR_MPI_INTEGER8           MPI_INTEGER8
#define UCS_VECTOR_MPI_INTEGER16          MPI_INTEGER16
#define UCS_VECTOR_MPI_REAL               MPI_REAL
#define UCS_VECTOR_MPI_REAL4              MPI_REAL4
#define UCS_VECTOR_MPI_REAL8              MPI_REAL8
#define UCS_VECTOR_MPI_REAL16             MPI_REAL16
#define UCS_VECTOR_MPI_DOUBLE_PRECISION   MPI_DOUBLE_PRECISION
#define UCS_VECTOR_MPI_COMPLEX            MPI_COMPLEX
#define UCS_VECTOR_MPI_COMPLEX8           MPI_COMPLEX8
#define UCS_VECTOR_MPI_COMPLEX16          MPI_COMPLEX16
#define UCS_VECTOR_MPI_COMPLEX32          MPI_COMPLEX32
#define UCS_VECTOR_MPI_DOUBLE_COMPLEX     MPI_DOUBLE_COMPLEX
#define UCS_VECTOR_MPI_2REAL              MPI_2REAL
#define UCS_VECTOR_MPI_2DOUBLE_PRECISION  MPI_2DOUBLE_PRECISION
#define UCS_VECTOR_MPI_2INTEGER           MPI_2INTEGER
#define UCS_VECTOR_MPI_2COMPLEX           MPI_2COMPLEX
#define UCS_VECTOR_MPI_2DOUBLE_COMPLEX    MPI_2DOUBLE_COMPLEX
#define UCS_VECTOR_MPI_REAL2              MPI_REAL2
#define UCS_VECTOR_MPI_LOGICAL1           MPI_LOGICAL1
#define UCS_VECTOR_MPI_LOGICAL2           MPI_LOGICAL2
#define UCS_VECTOR_MPI_LOGICAL4           MPI_LOGICAL4
#define UCS_VECTOR_MPI_LOGICAL8           MPI_LOGICAL8
#define UCS_VECTOR_MPI_WCHAR              MPI_WCHAR
#define UCS_VECTOR_MPI_CHAR               MPI_CHAR
#define UCS_VECTOR_MPI_UNSIGNED_CHAR      MPI_UNSIGNED_CHAR
#define UCS_VECTOR_MPI_SIGNED_CHAR        MPI_SIGNED_CHAR
#define UCS_VECTOR_MPI_SHORT              MPI_SHORT
#define UCS_VECTOR_MPI_UNSIGNED_SHORT     MPI_UNSIGNED_SHORT
#define UCS_VECTOR_MPI_INT                MPI_INT
#define UCS_VECTOR_MPI_UNSIGNED           MPI_UNSIGNED
#define UCS_VECTOR_MPI_LONG               MPI_LONG
#define UCS_VECTOR_MPI_UNSIGNED_LONG      MPI_UNSIGNED_LONG
#define UCS_VECTOR_MPI_LONG_LONG_INT      MPI_LONG_LONG_INT
#define UCS_VECTOR_MPI_UNSIGNED_LONG_LONG MPI_UNSIGNED_LONG_LONG
#define UCS_VECTOR_MPI_FLOAT              MPI_FLOAT
#define UCS_VECTOR_MPI_DOUBLE             MPI_DOUBLE
#define UCS_VECTOR_MPI_LONG_DOUBLE        MPI_LONG_DOUBLE
#define UCS_VECTOR_MPI_FLOAT_INT          MPI_FLOAT_INT
#define UCS_VECTOR_MPI_DOUBLE_INT         MPI_DOUBLE_INT
#define UCS_VECTOR_MPI_LONGDBL_INT        MPI_LONGDBL_INT
#define UCS_VECTOR_MPI_LONG_INT           MPI_LONG_INT
#define UCS_VECTOR_MPI_2INT               MPI_2INT
#define UCS_VECTOR_MPI_SHORT_INT          MPI_SHORT_INT
#define UCS_VECTOR_MPI_CXX_BOOL           MPI_CXX_BOOL
#define UCS_VECTOR_MPI_CXX_CPLEX          MPI_CXX_CPLEX
#define UCS_VECTOR_MPI_CXX_DBLCPLEX       MPI_CXX_DBLCPLEX
#define UCS_VECTOR_MPI_CXX_LDBLCPLEX      MPI_CXX_LDBLCPLEX
#define UCS_VECTOR_MPI_INT8_T             MPI_INT8_T
#define UCS_VECTOR_MPI_UINT8_T            MPI_UINT8_T
#define UCS_VECTOR_MPI_INT16_T            MPI_INT16_T
#define UCS_VECTOR_MPI_UINT16_T           MPI_UINT16_T
#define UCS_VECTOR_MPI_INT32_T            MPI_INT32_T
#define UCS_VECTOR_MPI_UINT32_T           MPI_UINT32_T
#define UCS_VECTOR_MPI_INT64_T            MPI_INT64_T
#define UCS_VECTOR_MPI_UINT64_T           MPI_UINT64_T
#define UCS_VECTOR_MPI_MAX_DTYPE          MPI_UINT64_T+1

#define UCS_VECTOR_MPI_MAX                MPI_MAX
#define UCS_VECTOR_MPI_MIN                MPI_MIN
#define UCS_VECTOR_MPI_SUM                MPI_SUM
#define UCS_VECTOR_MPI_PROD               MPI_PROD
#define UCS_VECTOR_MPI_LAND               MPI_LAND
#define UCS_VECTOR_MPI_BAND               MPI_BAND
#define UCS_VECTOR_MPI_LOR                MPI_LOR
#define UCS_VECTOR_MPI_BOR                MPI_BOR
#define UCS_VECTOR_MPI_LXOR               MPI_LXOR
#define UCS_VECTOR_MPI_BXOR               MPI_BXOR
#define UCS_VECTOR_MPI_MINLOC             MPI_MINLOC
#define UCS_VECTOR_MPI_MAXLOC             MPI_MAXLOC
#define UCS_VECTOR_MPI_REPLACE            MPI_REPLACE
#define UCS_VECTOR_MPI_MAX_OP             MPI_REPLACE+1
#endif

#define MIPP_STATIC_CHECK(_ctype, _dtype) \
switch (0) {case 0: case (sizeof(_ctype) == sizeof(_dtype)):;}; /* check length */ \
switch (0) {case 0: case ((_ctype)-1 == (_dtype)-1):;};         /* check signedness*/

#define MIPP_AGGREGATOR_NAME_(_op, _ctype, _dtype, _size) \
(ucs_vector_mpi_op_f)mipp::memreduce_<_dtype, mipp::_op<_dtype>, \
    mipp::_op<_dtype>>; MIPP_STATIC_CHECK(_ctype, _dtype)

#define MIPP_AGGREGATOR_NAME_unaligned_(_op, _ctype, _dtype, _size) \
(ucs_vector_mpi_op_f)mipp::memreduce_unaligned<_dtype, mipp::_op<_dtype>, \
    mipp::_op<_dtype>>; MIPP_STATIC_CHECK(_ctype, _dtype)

#define MIPP_AGGREGATOR_NAME_fixed_(_op, _ctype, _dtype, _size) \
(ucs_vector_mpi_op_f)mipp::memreduce_fixed<_size, _dtype, mipp::_op<_dtype>, \
    mipp::_op<_dtype>>; MIPP_STATIC_CHECK(_ctype, _dtype)

#define UCS_VECTOR_MPI_OP_SLOT(_name, _op_id, _dtype_id, _type) \
    ucs_vector_mpi_op_table ## _type ## _name [_op_id][_dtype_id]

#define UCS_VECTOR_MPI_SET_DATATYPES(_name, _op_id, _op, _type, _size) \
UCS_VECTOR_MPI_OP_SLOT(_name, _op_id, UCS_VECTOR_MPI_BYTE, _type)               = MIPP_AGGREGATOR_NAME ## _type (_op, char,                   int8_t,   _size); \
UCS_VECTOR_MPI_OP_SLOT(_name, _op_id, UCS_VECTOR_MPI_CHAR, _type)               = MIPP_AGGREGATOR_NAME ## _type (_op, char,                   int8_t,   _size); \
UCS_VECTOR_MPI_OP_SLOT(_name, _op_id, UCS_VECTOR_MPI_SHORT, _type)              = MIPP_AGGREGATOR_NAME ## _type (_op, signed short int,       int16_t,  _size); \
UCS_VECTOR_MPI_OP_SLOT(_name, _op_id, UCS_VECTOR_MPI_INT, _type)                = MIPP_AGGREGATOR_NAME ## _type (_op, signed int,             int32_t,  _size); \
UCS_VECTOR_MPI_OP_SLOT(_name, _op_id, UCS_VECTOR_MPI_LONG, _type)               = MIPP_AGGREGATOR_NAME ## _type (_op, signed long int,        int64_t,  _size); \
UCS_VECTOR_MPI_OP_SLOT(_name, _op_id, UCS_VECTOR_MPI_LONG_LONG_INT, _type)      = MIPP_AGGREGATOR_NAME ## _type (_op, signed long long int,   int64_t,  _size); \
UCS_VECTOR_MPI_OP_SLOT(_name, _op_id, UCS_VECTOR_MPI_SIGNED_CHAR, _type)        = MIPP_AGGREGATOR_NAME ## _type (_op, signed char,            int8_t,   _size); \
UCS_VECTOR_MPI_OP_SLOT(_name, _op_id, UCS_VECTOR_MPI_UNSIGNED_CHAR, _type)      = MIPP_AGGREGATOR_NAME ## _type (_op, unsigned char,          uint8_t,  _size); \
UCS_VECTOR_MPI_OP_SLOT(_name, _op_id, UCS_VECTOR_MPI_UNSIGNED_SHORT, _type)     = MIPP_AGGREGATOR_NAME ## _type (_op, unsigned short int,     uint16_t, _size); \
UCS_VECTOR_MPI_OP_SLOT(_name, _op_id, UCS_VECTOR_MPI_UNSIGNED, _type)           = MIPP_AGGREGATOR_NAME ## _type (_op, unsigned int,           uint32_t, _size); \
UCS_VECTOR_MPI_OP_SLOT(_name, _op_id, UCS_VECTOR_MPI_UNSIGNED_LONG, _type)      = MIPP_AGGREGATOR_NAME ## _type (_op, unsigned long int,      uint64_t, _size); \
UCS_VECTOR_MPI_OP_SLOT(_name, _op_id, UCS_VECTOR_MPI_UNSIGNED_LONG_LONG, _type) = MIPP_AGGREGATOR_NAME ## _type (_op, unsigned long long int, uint64_t, _size); \
UCS_VECTOR_MPI_OP_SLOT(_name, _op_id, UCS_VECTOR_MPI_FLOAT, _type)              = MIPP_AGGREGATOR_NAME ## _type (_op, float,                  float,    _size); \
UCS_VECTOR_MPI_OP_SLOT(_name, _op_id, UCS_VECTOR_MPI_DOUBLE, _type)             = MIPP_AGGREGATOR_NAME ## _type (_op, double,                 double,   _size); \
UCS_VECTOR_MPI_OP_SLOT(_name, _op_id, UCS_VECTOR_MPI_WCHAR, _type)              = MIPP_AGGREGATOR_NAME ## _type (_op, wchar_t,                uint32_t, _size); \
UCS_VECTOR_MPI_OP_SLOT(_name, _op_id, UCS_VECTOR_MPI_INT8_T, _type)             = MIPP_AGGREGATOR_NAME ## _type (_op, int8_t,                 int8_t,   _size); \
UCS_VECTOR_MPI_OP_SLOT(_name, _op_id, UCS_VECTOR_MPI_INT16_T, _type)            = MIPP_AGGREGATOR_NAME ## _type (_op, int16_t,                int16_t,  _size); \
UCS_VECTOR_MPI_OP_SLOT(_name, _op_id, UCS_VECTOR_MPI_INT32_T, _type)            = MIPP_AGGREGATOR_NAME ## _type (_op, int32_t,                int32_t,  _size); \
UCS_VECTOR_MPI_OP_SLOT(_name, _op_id, UCS_VECTOR_MPI_INT64_T, _type)            = MIPP_AGGREGATOR_NAME ## _type (_op, int64_t,                int64_t,  _size); \
UCS_VECTOR_MPI_OP_SLOT(_name, _op_id, UCS_VECTOR_MPI_UINT8_T, _type)            = MIPP_AGGREGATOR_NAME ## _type (_op, uint8_t,                uint8_t,  _size); \
UCS_VECTOR_MPI_OP_SLOT(_name, _op_id, UCS_VECTOR_MPI_UINT16_T, _type)           = MIPP_AGGREGATOR_NAME ## _type (_op, uint16_t,               uint16_t, _size); \
UCS_VECTOR_MPI_OP_SLOT(_name, _op_id, UCS_VECTOR_MPI_UINT32_T, _type)           = MIPP_AGGREGATOR_NAME ## _type (_op, uint32_t,               uint32_t, _size); \
UCS_VECTOR_MPI_OP_SLOT(_name, _op_id, UCS_VECTOR_MPI_UINT64_T, _type)           = MIPP_AGGREGATOR_NAME ## _type (_op, uint64_t,               uint64_t, _size);

#define UCS_VECTOR_MPI_IS_PTR_ALIGNED(p) ((uintptr_t)(p) % (MIPP_REGISTER_SIZE / 8) == 0)

#define UCS_VECTOR_MPI_SET_TABLES(_name, _op_id, _op,              _fixed_size) \
     UCS_VECTOR_MPI_SET_DATATYPES(_name, _op_id, _op, _,           _fixed_size) \
     UCS_VECTOR_MPI_SET_DATATYPES(_name, _op_id, _op, _fixed_,     _fixed_size) \
     UCS_VECTOR_MPI_SET_DATATYPES(_name, _op_id, _op, _unaligned_, _fixed_size)

#define UCG_VECTOR_FIXED_DECLARE_ONE(_size_in_bytes, _action)                  \
static inline int                                                              \
ucs_vector_fixed_ ## _action ## _size_in_bytes(const void* x, const void* y)   \
{                                                                              \
    return mipp::mem ## _action ## _fixed<_size_in_bytes, int32_t>             \
        ((int32_t*)x, (int32_t*)y);                                            \
}

#define UCG_VECTOR_FIXED_DECLARE_ONE_SET(_size_in_bytes)                       \
static inline void                                                             \
ucs_vector_fixed_memset ## _size_in_bytes(void* ptr, int8_t content)           \
{                                                                              \
    mipp::memset_fixed<_size_in_bytes, int8_t>((int8_t*)ptr, content);         \
}

#define UCG_VECTOR_FIXED_DECLARE(_size_in_bytes)                               \
        UCG_VECTOR_FIXED_DECLARE_ONE_SET(_size_in_bytes)                       \
        UCG_VECTOR_FIXED_DECLARE_ONE(_size_in_bytes, move)                     \
        UCG_VECTOR_FIXED_DECLARE_ONE(_size_in_bytes, cmp)

#define UCG_VECTOR_FIXED_CALL(_size_in_bytes, _type) \
        ucs_vector_fixed_ ## _type ## _size_in_bytes

typedef void (*ucs_vector_mpi_op_f)(void *inout, const void *in, const uint32_t len);

#define UCS_VECTOR_DECLARE(_name, _size_in_bytes)                              \
ucs_vector_mpi_op_f ucs_vector_mpi_op_table_ ## _name                          \
    [UCS_VECTOR_MPI_MAX_OP][UCS_VECTOR_MPI_MAX_DTYPE] = {0};                   \
ucs_vector_mpi_op_f ucs_vector_mpi_op_table_unaligned_ ## _name                \
    [UCS_VECTOR_MPI_MAX_OP][UCS_VECTOR_MPI_MAX_DTYPE] = {0};                   \
ucs_vector_mpi_op_f ucs_vector_mpi_op_table_fixed_ ## _name                    \
    [UCS_VECTOR_MPI_MAX_OP][UCS_VECTOR_MPI_MAX_DTYPE] = {0};                   \
UCG_VECTOR_FIXED_DECLARE(_size_in_bytes)                                       \
                                                                               \
/* This function reduces two vectors of MPI datatypes (e.g. MPI_INT) */        \
/* using the given reduction operator (e.g. MPI_MAX).                */        \
static inline int ucs_vector_mpi_reduce_ ## _name(int is_full_fragment,        \
        void *mpi_op, void *src_buffer, void *dst_buffer,                      \
        unsigned dcount, void* mpi_datatype)                                   \
{                                                                              \
    unsigned op_id    = UCS_VECTOR_MPI_GET_OP_ID(mpi_op);                      \
    unsigned dtype_id = UCS_VECTOR_MPI_GET_DTYPE_ID(mpi_datatype);             \
    ucs_assert(op_id    < UCS_VECTOR_MPI_MAX_OP);                              \
    ucs_assert(dtype_id < UCS_VECTOR_MPI_MAX_DTYPE);                           \
                                                                               \
    ucs_vector_mpi_op_f op_f;                                                  \
    if (ucs_unlikely((!UCS_VECTOR_MPI_IS_PTR_ALIGNED(src_buffer)) ||           \
                     (!UCS_VECTOR_MPI_IS_PTR_ALIGNED(dst_buffer)))) {          \
        op_f = UCS_VECTOR_MPI_OP_SLOT(_name, op_id, dtype_id, _unaligned_);    \
    } else {                                                                   \
        if (is_full_fragment) {                                                \
            op_f = UCS_VECTOR_MPI_OP_SLOT(_name, op_id, dtype_id, _fixed_);    \
        } else {                                                               \
            op_f = UCS_VECTOR_MPI_OP_SLOT(_name, op_id, dtype_id, _);          \
        }                                                                      \
    }                                                                          \
                                                                               \
    if (ucs_unlikely(op_f == NULL)) {                                          \
        return -1; /* No appropriate aggregator function found */              \
    }                                                                          \
                                                                               \
    op_f(src_buffer, dst_buffer, dcount);                                      \
    return 0;                                                                  \
}                                                                              \
                                                                               \
/* This function is a place-holder for a future implementation of    */        \
/* interleaving. The function will be passed an array of pointers to */        \
/* buffers and their contents will be transposed.                    */        \
static inline int ucs_vector_mpi_transpose_ ## _name (void **src_buffers,      \
        unsigned src_cnt, unsigned dcount, void* mpi_datatype)                 \
{                                                                              \
    return -1; /* NOT YET IMPLEMENTED */                                       \
}                                                                              \
                                                                               \
/* This function is a place-holder for a future implementation of    */        \
/* interleaving. The function will be passed an array of pointers to */        \
/* source buffers and a pointer to a destination buffer to write the */        \
/* interleaved result into.                                          */        \
static inline int ucs_vector_mpi_interleave_ ## _name (void **src_buffers,     \
        unsigned src_cnt, void *dst_buffer, unsigned dcount,                   \
        void* mpi_datatype)                                                    \
{                                                                              \
    return -1; /* NOT YET IMPLEMENTED */                                       \
}

#define UCS_VECTOR_INIT(_name, _fixed_size) \
    UCS_VECTOR_MPI_SET_TABLES(_name, UCS_VECTOR_MPI_MAX,  max,  _fixed_size)   \
    UCS_VECTOR_MPI_SET_TABLES(_name, UCS_VECTOR_MPI_MIN,  min,  _fixed_size)   \
    UCS_VECTOR_MPI_SET_TABLES(_name, UCS_VECTOR_MPI_SUM,  add,  _fixed_size)   \
    UCS_VECTOR_MPI_SET_TABLES(_name, UCS_VECTOR_MPI_PROD, mul,  _fixed_size)   \
    UCS_VECTOR_MPI_SET_TABLES(_name, UCS_VECTOR_MPI_LXOR, xorl, _fixed_size)   \
    UCS_VECTOR_MPI_SET_TABLES(_name, UCS_VECTOR_MPI_BXOR, xorb, _fixed_size)   \
    UCS_VECTOR_MPI_SET_TABLES(_name, UCS_VECTOR_MPI_LAND, andl, _fixed_size)   \
    UCS_VECTOR_MPI_SET_TABLES(_name, UCS_VECTOR_MPI_BAND, andb, _fixed_size)   \
    UCS_VECTOR_MPI_SET_TABLES(_name, UCS_VECTOR_MPI_LOR,  orl,  _fixed_size)   \
    UCS_VECTOR_MPI_SET_TABLES(_name, UCS_VECTOR_MPI_BOR,  orb,  _fixed_size)
