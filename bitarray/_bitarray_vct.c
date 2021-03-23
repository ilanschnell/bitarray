/* -------------------------------------------- term functions -------------------------------------------- */

static PyObject *
bitarray_eval_monic(bitarrayobject *self, PyObject *args, PyObject *kwds)
{
    static char* kwlist[] = {"data", "index", "blocksize", NULL};
    PyObject *x;
    Py_ssize_t index=0, blocksize=16, offset=0;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OLL:eval_monic", kwlist, &x, &index, &blocksize)) {
        return NULL;
    }

    if (!bitarray_Check(x)) {
        PyErr_SetString(PyExc_TypeError, "bitarray expected");
        return NULL;
    }

    if (index < 0){
        PyErr_SetString(PyExc_IndexError, "index has to be zero or greater");
        return NULL;
    }

    if (blocksize <= 0){
        PyErr_SetString(PyExc_IndexError, "block size has to be 1 or greater");
        return NULL;
    }

    if (index >= blocksize){
        PyErr_SetString(PyExc_IndexError, "index has to be strictly less than block size");
        return NULL;
    }

    // BEGIN Evaluation core:
    // Resize current bitarray so it can store the evaluation result.
    bitarrayobject * other = (bitarrayobject *) x;
    Py_ssize_t new_bit_size = other->nbits / blocksize;
    if (new_bit_size != self->nbits && resize(self, new_bit_size) < 0){
        return NULL;
    }

    setunused(self);

    // Actual term evaluation.
    // Naively it is same as the following: take index-th bit, skip blocksize bits.
    Py_ssize_t ctr = 0;
    char acc = 0;               // accumulator - here is the sub-result collected.
    unsigned char sub_ctr = 0;      // counter inside the char, small range
    for(offset = index; offset < other->nbits; offset += blocksize){
        if (GETBIT(other, offset)) {
            acc |= BITMASK(self->endian, sub_ctr);
        }

        ++sub_ctr;
        // Once accumulator is full (or this is the last iteration), flush it to
        // the buffer.
        if (sub_ctr >= 8 || offset + blocksize >= other->nbits){
            self->ob_item[ctr] = acc;
            sub_ctr = 0;
            acc = 0;
            ++ctr;
        }
    }

    // END Evaluation core
    Py_INCREF(self);
    return (PyObject *) self;
}

PyDoc_STRVAR(eval_monic_doc,
"eval_monic(data, index, blocksize)\n\
 \n\
 Evaluates a monic term on the input data with x_index and the given\n\
 blocksize. Equivalent to data[index::blocksize]. The evaluation is performed in-place with minimal \n\
 memory reallocations. The result is a bitarray of evaluations of the term.");

static PyObject *
bitarray_fast_copy(bitarrayobject *self, PyObject *obj)
{
    if (!bitarray_Check(obj)) {
        PyErr_SetString(PyExc_TypeError, "bitarray expected");
        return NULL;
    }

    bitarrayobject * other = (bitarrayobject *) obj;
    if (other->endian != self->endian){
        PyErr_SetString(PyExc_ValueError, "The source does not have the same endianity as the destination");
        return NULL;
    }

    if (other->nbits != self->nbits){
        PyErr_SetString(PyExc_ValueError, "The source does not have the same size as the destination");
        return NULL;
    }

    if (other == self || other->ob_item == self->ob_item){
        PyErr_SetString(PyExc_ValueError, "The source and the destination are the same");
        return NULL;
    }

    // Copy itself, very fast.
    setunused(self);
    setunused(other);
    copy_n(self, 0, other, 0, other->nbits);

    Py_INCREF(self);
    return (PyObject *) self;
}

PyDoc_STRVAR(fast_copy_doc,
"fast_copy(other_bitarray)\n\
 \n\
 Copies the contents of the parameter with memcpy. Has to have same endianness, size, ...");

#define BITWISE_HW_TYPE uint64_t

#define BITWISE_HW_INTERNAL(SELF, OTHER, OP) do {                                                                       \
     Py_ssize_t i = 0, ii = 0;                                                                                          \
     const Py_ssize_t size = Py_SIZE(SELF);                                                                             \
     const BITWISE_HW_TYPE * self_ob_item = (const BITWISE_HW_TYPE *) (SELF)->ob_item;                                  \
     const BITWISE_HW_TYPE * other_ob_item = (const BITWISE_HW_TYPE *) (OTHER)->ob_item;                                \
     BITWISE_HW_TYPE tmp;                                                                                               \
                                                                                                                        \
     for (ii=0; (Py_ssize_t)(i + sizeof(BITWISE_HW_TYPE)) < size; i += (Py_ssize_t)sizeof(BITWISE_HW_TYPE), ++ii) {     \
         tmp = self_ob_item[ii] OP other_ob_item[ii];                                                                   \
         BITWISE_HW_WP3(tmp, hw);                                                                                       \
     }                                                                                                                  \
                                                                                                                        \
     for (; i < size; ++i) {                                                                                            \
         hw += bitcount_lookup[(unsigned char)((SELF)->ob_item[i] OP (OTHER)->ob_item[i])];                             \
     }                                                                                                                  \
 } while(0)

#define BITWISE_FAST_HW_FUNC(OPNAME, OP)                                                                                \
 static PyObject * bitwise_fast_hw_ ## OPNAME (bitarrayobject *self, PyObject *obj)                                     \
 {                                                                                                                      \
     if (!bitarray_Check(obj)) {                                                                                        \
         PyErr_SetString(PyExc_TypeError, "bitarray expected");                                                         \
         return NULL;                                                                                                   \
     }                                                                                                                  \
                                                                                                                        \
     bitarrayobject * other = (bitarrayobject *) obj;                                                                   \
     if (other->endian != self->endian){                                                                                \
         PyErr_SetString(PyExc_ValueError, "The source does not have the same endianity as the destination");           \
         return NULL;                                                                                                   \
     }                                                                                                                  \
                                                                                                                        \
     if (other->nbits != self->nbits){                                                                                  \
         PyErr_SetString(PyExc_ValueError, "The source does not have the same size as the destination");                \
         return NULL;                                                                                                   \
     }                                                                                                                  \
                                                                                                                        \
     if (other->nbits == 0) return PyLong_FromLongLong(0);                                                              \
     if (other == self || other->ob_item == self->ob_item){                                                             \
         PyErr_SetString(PyExc_ValueError, "The source and the destination are the same");                              \
         return NULL;                                                                                                   \
     }                                                                                                                  \
                                                                                                                        \
     Py_ssize_t hw = 0;                                                                                                 \
     setunused(self);                                                                                                   \
     setunused(other);                                                                                                  \
                                                                                                                        \
     BITWISE_HW_INTERNAL(self, other, OP);                                                                              \
     return PyLong_FromLongLong(hw);                                                                                    \
 }

BITWISE_FAST_HW_FUNC(and, &);
BITWISE_FAST_HW_FUNC(xor, ^);
BITWISE_FAST_HW_FUNC(or, |);

PyDoc_STRVAR(bitwise_fast_hw_and_doc,
"fast_hw_and(other_bitarray)\n\
 \n\
 Performs quick in-memory AND operation on these self and other_bitarray and returns a hamming weight.");

PyDoc_STRVAR(bitwise_fast_hw_or_doc,
"fast_hw_or(other_bitarray)\n\
 \n\
 Performs quick in-memory OR operation on these self and other_bitarray and returns a hamming weight.");

PyDoc_STRVAR(bitwise_fast_hw_xor_doc,
"fast_hw_xor(other_bitarray)\n\
 \n\
 Performs quick in-memory XOR operation on these self and other_bitarray and returns a hamming weight.");

/* -------------------------------------------- bitarray repr -------------------------------------------- */


#if HAS_VECTORS
#define BITWISE_FUNC_INTERNAL(SELF, OTHER, OP, OPEQ) do {   \
     Py_ssize_t i = 0;                                       \
     const Py_ssize_t size = Py_SIZE(SELF);                  \
     char* self_ob_item = (SELF)->ob_item;                   \
     const char* other_ob_item = (OTHER)->ob_item;           \
                                                             \
     for (; (Py_ssize_t)(i + sizeof(vec)) < size; i += sizeof(vec)) {      \
         vector_op(self_ob_item + i, other_ob_item + i, OP); \
     }                                                       \
                                                             \
     for (; i < size; ++i) {                                 \
         self_ob_item[i] OPEQ other_ob_item[i];              \
     }                                                       \
 } while(0);
#else
#define BITWISE_FUNC_INTERNAL(SELF, OTHER, OP, OPEQ) do { \
     Py_ssize_t i;                                         \
     const Py_ssize_t size = Py_SIZE(SELF);                \
                                                           \
     for (i = 0; i < size; ++i) {                          \
         (SELF)->ob_item[i] OPEQ (OTHER)->ob_item[i];      \
     }                                                     \
 } while(0);
#endif


/* -------------------------------------------- best terms -------------------------------------------- */

// Heap element
typedef struct {
    int64_t hwdiff;
    int64_t hw;
    uint64_t idx;
} topterm_heap_elem_t;

static int topterm_compare(const void *e1, const void *e2, const void *udata ATTR_UNUSED)
{
    UNUSEDVAR(udata);
    const topterm_heap_elem_t *i1 = e1;
    const topterm_heap_elem_t *i2 = e2;
    if (i2->hwdiff == i1->hwdiff){
        return 0;
    }
    return i1->hwdiff < i2->hwdiff ? 1 : -1;
}

// heap implementation: https://github.com/ph4r05/heap
typedef struct heap_s{
    unsigned int size;  /* size of array */
    unsigned int count; /* items within heap */
    const void *udata;  /* user data */
    int (*cmp) (const void *, const void *, const void *);
    void * array[];
} heap_t;

size_t heap_sizeof(unsigned int size)
{
    return sizeof(heap_t) + size * sizeof(void *);
}

#define HP_CHILD_LEFT(idx) ((idx) * 2 + 1)
#define HP_CHILD_RIGHT(idx) ((idx) * 2 + 2)
#define HP_PARENT(idx) (((idx) - 1) / 2)

void heap_init(heap_t* h, int (*cmp) (const void *,const void *, const void *udata), const void *udata, unsigned int size)
{
    h->cmp = cmp;
    h->udata = udata;
    h->size = size;
    h->count = 0;
}

heap_t *heap_new(int (*cmp) (const void *, const void *, const void *udata), const void *udata, unsigned int size)
{
    heap_t *h = PyMem_Malloc(heap_sizeof(size));

    if (!h)
        return NULL;

    heap_init(h, cmp, udata, size);

    return h;
}

void heap_free(heap_t * h)
{
    PyMem_Free(h);
}

int heap_count(const heap_t * h)
{
    return h->count;
}

/**
 * @return a new heap on success; NULL otherwise */
static heap_t* __ensurecapacity(heap_t * h)
{
    if (h->count < h->size)
        return h;

    h->size *= 2;

    return PyMem_Realloc(h, heap_sizeof(h->size));
}

static void __swap(heap_t * h, const int i1, const int i2)
{
    void *tmp = h->array[i1];
    h->array[i1] = h->array[i2];
    h->array[i2] = tmp;
}

static int __pushup(heap_t * h, unsigned int idx)
{
    /* 0 is the root node */
    while (0 != idx)
    {
        int parent = HP_PARENT(idx);

        /* we are smaller than the parent */
        if (h->cmp(h->array[idx], h->array[parent], h->udata) < 0)
            return -1;
        else
            __swap(h, idx, parent);

        idx = parent;
    }

    return idx;
}

static void __pushdown(heap_t * h, unsigned int idx)
{
    while (1)
    {
        unsigned int childl, childr, child;

        childl = HP_CHILD_LEFT(idx);
        childr = HP_CHILD_RIGHT(idx);

        if (childr >= h->count)
        {
            /* can't pushdown any further */
            if (childl >= h->count)
                return;

            child = childl;
        }
            /* find biggest child */
        else if (h->cmp(h->array[childl], h->array[childr], h->udata) < 0)
            child = childr;
        else
            child = childl;

        /* idx is smaller than child */
        if (h->cmp(h->array[idx], h->array[child], h->udata) < 0)
        {
            __swap(h, idx, child);
            idx = child;
            /* bigger than the biggest child, we stop, we win */
        }
        else
            return;
    }
}

static void __heap_offerx(heap_t * h, void *item)
{
    h->array[h->count] = item;
    __pushup(h, h->count++);
}

int heap_offerx(heap_t * h, void *item)
{
    if (h->count == h->size)
        return -1;
    __heap_offerx(h, item);
    return 0;
}

int heap_offer(heap_t ** h, void *item)
{
    if (NULL == (*h = __ensurecapacity(*h)))
        return -1;

    __heap_offerx(*h, item);
    return 0;
}

/* -------------------------------------------- term generator -------------------------------------------- */
#define MAX_DEG 64
typedef struct {
    int deg;
    int maxterm;
    int cur[MAX_DEG];
} termgen_t;


/**
 * Initializes term generator to a first combination
 */
static void init_termgen(termgen_t * t, int deg, int maxterm){
    int i;
    t->deg = deg;
    t->maxterm = maxterm;
    memset(t->cur, 0, sizeof(int)*MAX_DEG);
    for(i=0; i<deg; i++){
        t->cur[i] = i;
    }
}

/**
 * Moves termgen to a next combination
 */
static int next_termgen(termgen_t * t){
    int j;
    int idx = t->deg - 1;

    if (t->cur[idx] == t->maxterm - 1) {
        do {
            idx -= 1;
        } while (idx >= 0 && t->cur[idx] + 1 == t->cur[idx + 1]);

        if (idx < 0) {
            return 0;
        }

        for (j = idx + 1; j < t->deg; ++j) {
            t->cur[j] = t->cur[idx] + j - idx + 1;
        }
    }

    t->cur[idx]++;
    return 1;
}

#define OP_AND 1
#define OP_XOR 2

/*********************** (Bitarray) Base object *********************/

#define POLY_DYNAMIC 0
#define MAX_POLY_DEG 12
#define MAX_POLY_TERMS 12

typedef struct tpoly {
    Py_ssize_t nterms;
    Py_ssize_t mterm_ord;
    Py_ssize_t maxterm;
#if POLY_DYNAMIC
    Py_ssize_t * sizes;
    Py_ssize_t * poly;        // polynomial, array of term indices
#else
    Py_ssize_t sizes[MAX_POLY_TERMS];
    Py_ssize_t poly[MAX_POLY_DEG * MAX_POLY_TERMS];        // polynomial, array of term indices
#endif
} tpoly;
static void tpoly_destroy_body(tpoly * poly);

typedef struct {
    PyObject_HEAD
    Py_ssize_t maxterm;
    Py_ssize_t base_size;
    char * valids;
    bitarrayobject ** base;

    Py_ssize_t eval_buff_size;
    struct tpoly * eval_buff;
    BITWISE_HW_TYPE * hws_buff;
} tbase;

static PyTypeObject TBase_Type;

#define TBase_Check(op)  PyObject_TypeCheck(op, &TBase_Type)

static void
tbase_destroy(tbase * base, int full_dealloc){
    if (base == NULL){
        return;
    }

    for (Py_ssize_t k = 0; base->base != NULL && k < base->maxterm; k++) {
        if (base->base[k] == NULL){
            continue;
        }
        Py_DECREF(base->base[k]);
        base->base[k] = NULL;
    }

    if (base->base != NULL){
        PyMem_Free((void *) base->base);
        base->base = NULL;
    }

    if (base->valids != NULL){
        PyMem_Free((void *) base->valids);
        base->valids = NULL;
    }

    if (base->eval_buff != NULL){
        for(Py_ssize_t i = 0; i < base->eval_buff_size; ++i){
            tpoly_destroy_body(&(base->eval_buff[i]));
        }
        PyMem_Free((void *) base->eval_buff);
        base->eval_buff = NULL;
    }

    if (base->hws_buff != NULL){
        PyMem_Free((void *) base->hws_buff);
        base->hws_buff = NULL;
    }

    if (full_dealloc)
        PyMem_Free((void *) base);
}

static int
tbase_get(PyObject * base_arr, tbase ** ibase){
    if (ibase == NULL){
        PyErr_SetString(PyExc_ValueError, "empty base pointer");
        return 0;
    }

    if (!PyList_Check(base_arr)) {
        PyErr_SetString(PyExc_TypeError, "base is expected as a list of bit arrays");
        return 0;
    }

    const int we_alloc = *ibase == NULL;
    if (*ibase == NULL){
        *ibase = PyMem_Malloc((size_t) (sizeof(tbase)));
        if (*ibase == NULL) {
            PyErr_NoMemory();
            return 0;
        }
    }

    tbase *base = *ibase;
    base->maxterm = PyList_Size(base_arr);
    base->base = NULL;
    base->valids = NULL;
    base->eval_buff = NULL;
    base->hws_buff = NULL;
    base->base_size = -1;
    base->eval_buff_size = 0;

    base->base = PyMem_Malloc((size_t) (sizeof(PyObject *) * base->maxterm));
    if (base->base == NULL) {
        PyErr_NoMemory();
        tbase_destroy(base, we_alloc);
        return 0;
    }

    memset(base->base, 0, (sizeof(PyObject *) * base->maxterm));
    base->valids = PyMem_Malloc((size_t) (sizeof(char) * base->maxterm));
    if (base->valids == NULL) {
        PyErr_NoMemory();
        tbase_destroy(base, we_alloc);
        return 0;
    }

    memset(base->valids, 0, (sizeof(char) * base->maxterm));

    int ok = 1;
    for (Py_ssize_t k = 0; k < base->maxterm; ++k) {
        PyObject * tmp_obj = PyList_GetItem(base_arr, (Py_ssize_t) k);
        if (tmp_obj == Py_None){
            continue;
        }

        if (!bitarray_Check(tmp_obj)) {
            PyErr_SetString(PyExc_TypeError, "bitarray expected in the base array");
            ok = 0;
            break;
        }

        base->valids[k] = 1;
        base->base[k] = (bitarrayobject *) tmp_obj;
        Py_INCREF(base->base[k]);
        if (base->base_size < 0){
            base->base_size = base->base[k]->nbits;

        } else if (base->base[k]->nbits != base->base_size){
            PyErr_Format(PyExc_ValueError, "Base size has to be the same, idx: %d", (int) k);
            ok = 0;
            break;
        }
    }

    if (!ok){
        tbase_destroy(base, we_alloc);
        return 0;
    } else {
        return 1;
    }
}

static int
tbase_traverse(tbase *base, visitproc visit, void *arg)
{
    for (Py_ssize_t k = 0; base->base != NULL && k < base->maxterm; k++) {
        if (base->base[k] == NULL){
            continue;
        }
        Py_VISIT(base->base[k]);
    }
    return 0;
}

static void
tbase_dealloc(tbase *it)
{
    PyObject_GC_UnTrack(it);
    tbase_destroy(it, 0);
    Py_TYPE(it)->tp_free((PyObject *)it);
}

static int
tbase_eval_buff(tbase *base, PyObject * num){
    if (!PyNumber_Check(num)){
        return 1;
    }

    Py_ssize_t k = PyNumber_AsSsize_t(num, NULL);
    if (k <= 0){
        return 1;
    }

    base->eval_buff_size = 0;
    base->eval_buff = PyMem_Malloc((size_t) (sizeof(tpoly) * k));
    if (base->eval_buff == NULL) {
        PyErr_NoMemory();
        return 0;
    }

    base->hws_buff = PyMem_Malloc((size_t) (sizeof(BITWISE_HW_TYPE) * k));
    if (base->hws_buff == NULL) {
        PyMem_Free((void *) base->eval_buff);
        PyErr_NoMemory();
        return 0;
    }

    base->eval_buff_size = k;
    return 1;
}

static PyObject *
tbase_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *ibase = NULL;
    PyObject *nbuff = NULL;
    static char *kwlist[] = {"base", "buff_size", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OO:tbase", kwlist, &ibase, &nbuff))
        return NULL;

    assert(type != NULL && type->tp_alloc != NULL);
    tbase * obj = (tbase *) type->tp_alloc(type, 0);
    if (obj == NULL) {
        PyErr_NoMemory();
        return NULL;
    }

    // https://docs.python.org/3/c-api/gcsupport.html
    // https://github.com/python/cpython/blob/master/Objects/dictobject.c
    // Whole object has to be valid when tracking. Thus untrack, setup, track again.
    PyObject_GC_UnTrack(obj);

    if (!tbase_get(ibase, &obj)){
        Py_DECREF(obj);
        return NULL;
    }

    if (!tbase_eval_buff(obj, nbuff)){
        Py_DECREF(obj);
        return NULL;
    }

    PyObject_GC_Track(obj);
    return (PyObject *) obj;
}

/*********************** Eval all terms *********************/

// eval_top_k. We need a simple heap - heap allocated top 128 elements
static PyObject *
eval_all_terms(PyObject *self, PyObject *args, PyObject *kwds)
{
    static char* kwlist[] = {"base", "deg", "topk", "hw_center", NULL};
    PyObject *base_arr;
    Py_ssize_t deg=2, topk=128, hw_center=0;
    long k = 0;
    int op_code = OP_AND;
    topterm_heap_elem_t * heap_data = NULL;

    //PySys_WriteStdout("Here! %p ");
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|LLL:eval_all_terms", kwlist, &base_arr, &deg, &topk, &hw_center)) {
        return NULL;
    }

    if (deg < 2){
        PyErr_SetString(PyExc_IndexError, "Minimal degree is 2. For 1 use directly hw()");
        return NULL;
    }

    // Base checking. Allocate memory for the base of maxterms.
    tbase * base = NULL;
    if (!tbase_get(base_arr, &base)){
        PyErr_NoMemory();
        return NULL;
    }

    if (deg > base->maxterm){
        PyErr_SetString(PyExc_IndexError, "degree is larger than size of the base");
        tbase_destroy(base, 1);
        return NULL;
    }

    // Allocate memory for the heap objects, actual storage of the heap structs. Heap contains only pointers.
    heap_data = PyMem_Malloc((size_t) (sizeof(topterm_heap_elem_t) * topk));
    if (heap_data == NULL){
        PyErr_NoMemory();
        goto error;
    }

    // Prepare state & working variables
    //const Py_ssize_t size = Py_SIZE(base[0]);
    Py_ssize_t hw = 0, comb_idx=-1;

    // Sub result for deg-1
    int last_cached_tmpsub = -1;
    bitarrayobject *tmpsub = (bitarrayobject *) newbitarrayobject(Py_TYPE(base->base[0]), base->base[0]->nbits, base->base[0]->endian);
    if (tmpsub == NULL) {
        goto error;
    }

    // Create the heap
    heap_t *hp = heap_new(topterm_compare, NULL, (unsigned int) topk);

    // Generate all polynomials
    termgen_t termgen;
    init_termgen(&termgen, (int)deg, (int)base->maxterm);
    do {
        hw = 0;
        comb_idx += 1;
        const int idx_first = termgen.cur[0];
        const int idx_plast = termgen.cur[deg-2];
        const int idx_last = termgen.cur[deg-1];

        // Precompute tmpsub up to deg-2.
        // Copy the first basis vector. Then AND or XOR next basis vector from the term.
        if (last_cached_tmpsub != idx_plast){
            memcpy(tmpsub->ob_item, base->base[idx_first]->ob_item, Py_SIZE(base->base[idx_first]));
            for(k=1; k <= deg-2; k++){
                if (op_code == OP_AND) {
                    BITWISE_FUNC_INTERNAL(tmpsub, base->base[termgen.cur[k]], &, &=);
                } else if (op_code == OP_XOR){
                    BITWISE_FUNC_INTERNAL(tmpsub, base->base[termgen.cur[k]], ^, ^=);
                }
            }

            last_cached_tmpsub = idx_plast;
        }

        // Now do just in-memory HW counting.
        if (op_code == OP_AND) {
            BITWISE_HW_INTERNAL(tmpsub, base->base[idx_last], &);
        } else if (op_code == OP_XOR){
            BITWISE_HW_INTERNAL(tmpsub, base->base[idx_last], ^);
        }

        int64_t hw_diff = hw_center > hw ? (hw_center - hw) : (hw - hw_center);

        // Not interesting element (too low & heap is full already) -> continue.
        if (comb_idx >= hp->size && hw_diff <= ((topterm_heap_elem_t * )(hp->array[0]))->hwdiff){
            continue;
        }

        // Interesting element.
        if (comb_idx < hp->size){
            heap_data[comb_idx].hwdiff = hw_diff;
            heap_data[comb_idx].hw = hw;
            heap_data[comb_idx].idx = (uint64_t)comb_idx;
            heap_offerx(hp, &(heap_data[comb_idx]));

        } else {
            topterm_heap_elem_t * const being_replaced = (topterm_heap_elem_t *) hp->array[0];
            being_replaced->hwdiff = hw_diff;
            being_replaced->hw = hw;
            being_replaced->idx = (uint64_t)comb_idx;
            __pushdown(hp, 0);
        }

    } while(next_termgen(&termgen) == 1);

    // Return as an array of tuples. Sorting will be done in the python already.
    PyObject* ret_list = PyList_New((Py_ssize_t)hp->count);
    for(k=0; k < hp->count; k++){
        topterm_heap_elem_t * const cur = (topterm_heap_elem_t *) hp->array[k];
        PyObject* cur_tuple = PyTuple_Pack(3,
                                           PyLong_FromLongLong(cur->hwdiff),
                                           PyLong_FromLongLong(cur->hw),
                                           PyLong_FromLongLong(cur->idx));
        PyList_SetItem(ret_list, (Py_ssize_t)k, cur_tuple);
    }

    heap_free(hp);
    tbase_destroy(base, 1);
    if (heap_data != NULL){
        PyMem_Free((void *) heap_data);
    }

    return ret_list;

    error:
    tbase_destroy(base, 1);
    if (heap_data != NULL){
        PyMem_Free((void *) heap_data);
    }
    return NULL;
}

PyDoc_STRVAR(eval_all_terms_doc,
"eval_all_terms(base, deg=2, topk=128, hw_center=0) -> list of (hwdiff, hw, idx)\n\
 \n\
 Evaluates all terms on the given basis");


static void
tpoly_destroy_body(tpoly * poly){
#if POLY_DYNAMIC
    if (poly == NULL){
        return;
    }

    if (poly->sizes){
        PyMem_Free((void *) poly->sizes);
        poly->sizes = NULL;
    }

    if (poly->poly){
        PyMem_Free((void *) poly->poly);
        poly->poly = NULL;
    }
#endif
}

static int
tpoly_get(PyObject * poly_arr, tpoly * poly, int fast){
    Py_ssize_t (* const ph4_size)(PyObject *o) = PyList_Size;
    PyObject * (* const ph4_item)(PyObject *o, Py_ssize_t i) = PyList_GetItem;
#define PH4_DEC_ITEM(x)

    if (!PyList_Check(poly_arr)) {
        PyErr_SetString(PyExc_TypeError, "poly is expected to be list of lists of integers");
        return 0;
    }

    memset(poly, 0, sizeof(tpoly));
    poly->nterms = ph4_size(poly_arr);

#if POLY_DYNAMIC
    poly->sizes = PyMem_Malloc((size_t) (poly->nterms * sizeof(Py_ssize_t)));
    if (poly->sizes == NULL) {
        tpoly_destroy_body(poly);
        PyErr_NoMemory();
        return NULL;
    }
#endif

    int ok = 1;
    for (Py_ssize_t k = 0; k < poly->nterms && ok; ++k) {
        PyObject * tmp_obj = ph4_item(poly_arr, (Py_ssize_t) k);
        if (!fast && !PyList_Check(tmp_obj)) {
            PyErr_Format(PyExc_ValueError, "poly is expected to be list of lists of integers, idx: %d", (int) k);
            ok = 0;

        } else {
            const Py_ssize_t tsize = ph4_size(tmp_obj);
            poly->sizes[k] = tsize;
            poly->mterm_ord = tsize > poly->mterm_ord ? tsize : poly->mterm_ord;
        }
        PH4_DEC_ITEM(tmp_obj);
    }

    if (!ok){
        tpoly_destroy_body(poly);
        return 0;
    }

#if POLY_DYNAMIC
    poly->poly = PyMem_Malloc((size_t) (poly->nterms * poly->mterm_ord * sizeof(Py_ssize_t)));
    if (poly->poly == NULL) {
        tpoly_destroy_body(poly);
        PyErr_NoMemory();
        return NULL;
    }
#endif

    for (Py_ssize_t k = 0; k < poly->nterms && ok; ++k) {
        PyObject * tmp_obj = ph4_item(poly_arr, (Py_ssize_t) k);
        const Py_ssize_t tsize = ph4_size(tmp_obj);

        for (Py_ssize_t l = 0; l < tsize && ok; ++l) {
            PyObject * to2 = ph4_item(tmp_obj, (Py_ssize_t) l);
            if (!fast && !PyNumber_Check(to2)){
                PyErr_Format(PyExc_ValueError, "poly is expected to be list of lists of integers, idx: %d, %d", (int) k, (int) l);
                ok = 0;

            } else {
                const Py_ssize_t ct = PyNumber_AsSsize_t(to2, NULL);
                poly->poly[k * poly->mterm_ord + l] = ct;
                poly->maxterm = ct > poly->maxterm ? ct : poly->maxterm;
            }
            PH4_DEC_ITEM(to2);
        }
        PH4_DEC_ITEM(tmp_obj);
    }

    if (!ok){
        tpoly_destroy_body(poly);
        return 0;
    } else {
        return 1;
    }
#undef PH4_DEC_ITEM
}

static PyObject *
base_eval_polynomial_hw(tbase *base, PyObject *args, PyObject *kwds) {
    PyObject *poly_arr = NULL;
    PyObject *polys_arr = NULL;
    PyObject *res_arr = NULL;
    if (!PyArg_ParseTuple(args, "|OOO:eval_poly_hw", &poly_arr, &polys_arr, &res_arr)) {
        return NULL;
    }

    Py_ssize_t npolys = 1;
    tpoly bpolys[1];
    BITWISE_HW_TYPE bhws[1] = {0};

    tpoly * polys = (tpoly *) &bpolys;
    BITWISE_HW_TYPE * hws = &bhws[0];
    const int single_poly = poly_arr != NULL && poly_arr != Py_None;

    if (res_arr != NULL && res_arr != Py_None){
        if (!PyList_Check(res_arr)){
            PyErr_SetString(PyExc_ValueError, "res array has to be an array");
            goto error;
        }
    }

    if (single_poly){
        if (!tpoly_get(poly_arr, &polys[0], 1)) {
            goto error;
        }
    } else {
        polys = NULL;
        hws = NULL;
        if (!PyList_Check(polys_arr)){
            PyErr_SetString(PyExc_ValueError, "polys has to be array of polynomials");
            goto error;
        }

        npolys = PyList_Size(polys_arr);
        if (npolys == 0){
            return PyList_New(0);
        }

        if (npolys <= base->eval_buff_size){
            polys = base->eval_buff;
            hws = base->hws_buff;

        } else {
            polys = PyMem_Malloc((size_t) (npolys * sizeof(tpoly)));
            if (polys == NULL) {
                PyErr_NoMemory();
                goto error;
            }

            hws = PyMem_Malloc((size_t) (npolys * sizeof(BITWISE_HW_TYPE)));
            if (hws == NULL) {
                PyErr_NoMemory();
                goto error;
            }
        }

        memset(polys, 0, npolys * sizeof(tpoly));
        memset(hws, 0, npolys * sizeof(BITWISE_HW_TYPE));
        for(Py_ssize_t i = 0; i < npolys; ++i){
            if (!tpoly_get(PyList_GetItem(polys_arr, i), &polys[i], 1)) {
                goto error;
            }
        }
    }


//#define POLY_TERMS(p) (nterms)
//#define POLY_TERM_SIZE(p, k, t) (PyList_GET_SIZE(PyList_GET_ITEM(poly_arr, k)))
//#define POLY_TERM(p, k)  (PyList_GET_ITEM(poly_arr, k))
//#define POLY_TERM_FREE(p, k, t)
//#define POLY_VAR(p, k, l, t) (PyLong_AsSsize_t(PyList_GET_ITEM(t, l)))

#define POLY_TERMS(p) (polys[p].nterms)
#define POLY_TERM_SIZE(p, k, t) (polys[p].sizes[k])
#define POLY_TERM(p, k, name)
#define POLY_TERM_FREE(p, k, t)
#define POLY_IDX(p, k, l) ((k) * polys[p].mterm_ord + (l))
#define POLY_VAR(p, k, l, t) (polys[p].poly[POLY_IDX(p, k, l)])

#define POLY_OBJ(p, k, l, t) (base->base[POLY_VAR(p, k, l, t)])
#define POLY_DATA(p, k, l, t) ((POLY_OBJ(p, k, l, t))->ob_item)

    // BITWISE_HW_TYPE jumps
    Py_ssize_t off = 0;
    Py_ssize_t base_size_lim = 0;

#if HAS_VECTORS
    base_size_lim = base->base_size < (Py_ssize_t)sizeof(vec) ? 0 : (base->base_size >> 3) - ((base->base_size >> 3) % sizeof(vec));
    for(Py_ssize_t ii = 0; off < base_size_lim; ++ii, off += (Py_ssize_t)sizeof(vec)) {  // Base
        for(Py_ssize_t p = 0; p < npolys; ++p) {                         // polys
            vec res = bitv_00;
            for (Py_ssize_t k = 0; k < POLY_TERMS(p); ++k) {             // XOR
                vec subr = bitv_ff;
                POLY_TERM(p, k, term);
                for (Py_ssize_t l = 0; l < POLY_TERM_SIZE(p, k, term); ++l) {  // AND
                    vec ttt;
                    memcpy(&ttt, &(POLY_DATA(p, k, l, term)[off]), sizeof(vec));
                    subr &= ttt;
                }
                res ^= subr;
            }

            BITWISE_HW_TYPE ww = 0;
            memcpy(&ww, &res, 8);
            BITWISE_HW_WP3(ww, hws[p]);
            memcpy(&ww, ((char*)&res)+8, 8);
            BITWISE_HW_WP3(ww, hws[p]);
        }
    }
#endif

    // uint64_t finish
    base_size_lim = (base->base_size >> 3) - ((base->base_size >> 3) % sizeof(BITWISE_HW_TYPE));
    for(Py_ssize_t ii = 0; off < base_size_lim; ++ii, off += (Py_ssize_t)sizeof(BITWISE_HW_TYPE)) {  // Base
        for(Py_ssize_t p = 0; p < npolys; ++p) {                         // polys
            BITWISE_HW_TYPE res = 0;
            for (Py_ssize_t k = 0; k < POLY_TERMS(p); ++k) {             // XOR
                BITWISE_HW_TYPE subr = ~((BITWISE_HW_TYPE) 0);
                POLY_TERM(p, k, term);
                for (Py_ssize_t l = 0; l < POLY_TERM_SIZE(p, k, term); ++l) {  // AND
                    subr &= *(BITWISE_HW_TYPE *) (((char *) POLY_DATA(p, k, l, term)) + off);
                }
                res ^= subr;
            }
            BITWISE_HW_WP3(res, hws[p]);
        }
    }

    // Soft finish, bit-wise
    Py_ssize_t off_bits = off << 3;
    Py_ssize_t rem_bits = base->base_size - (off << 3);
    BITWISE_HW_TYPE res_mask = ((BITWISE_HW_TYPE)1 << (rem_bits)) - 1;
    for(Py_ssize_t p = 0; p < npolys; ++p) {                           // polys
        BITWISE_HW_TYPE res = 0;
        for (Py_ssize_t k = 0; k < POLY_TERMS(p); ++k) {               // XOR
            BITWISE_HW_TYPE subr = ~((BITWISE_HW_TYPE) 0);
            POLY_TERM(p, k, term);
            for (Py_ssize_t l = 0; l < POLY_TERM_SIZE(p, k, term); ++l) {    // AND
                BITWISE_HW_TYPE cur = 0;
                for (Py_ssize_t i = off_bits; i < base->base_size; ++i) {  // Base
                    cur |= (((BITWISE_HW_TYPE) GETBIT(POLY_OBJ(p, k, l, term), i)) << i);
                }
                subr &= cur;
            }
            res ^= subr;
        }
        res &= res_mask;
        BITWISE_HW_WP3(res, hws[p]);
    }

#define DEALLOC() \
    for(Py_ssize_t k = 0; polys != NULL && k < npolys; ++k)         \
        tpoly_destroy_body(&polys[k]);                              \
                                                                    \
    if (!single_poly && npolys > base->eval_buff_size){             \
        if (hws != NULL)                                            \
            PyMem_Free((void *) hws);                               \
        if (polys != NULL)                                          \
            PyMem_Free((void *) polys);                             \
    }

    // Return
    PyObject * ret = !single_poly ? NULL : PyLong_FromLongLong(hws[0]);

    if (single_poly) {
        DEALLOC()
        return ret;

    } else {
        if (res_arr == NULL || res_arr == Py_None){
            ret = PyList_New(npolys);
            if (ret == NULL){
                PyErr_NoMemory();
                goto error;
            }
            Py_INCREF(ret);
        } else {
            ret = res_arr;
            Py_INCREF(ret);
        }

        for(Py_ssize_t p = 0; p < npolys; ++p) {
            if (PyList_SetItem(ret, p, PyLong_FromLongLong(hws[p]))){
                PyErr_SetString(PyExc_ValueError, "Error adding result to the array");
                goto error;
            }
        }

        DEALLOC()
        return ret;
    }

    error:
DEALLOC()
    return NULL;

#undef STEPTYPE
#undef DEALLOC
#undef POLY_OBJ
#undef POLY_DATA
#undef POLY_VAR
#undef POLY_IDX
#undef POLY_TERM
#undef POLY_TERM_FREE
#undef POLY_TERMS
#undef POLY_TERM_SIZE
}

PyDoc_STRVAR(base_eval_polynomial_hw_doc,
             "eval_poly_hw(base, poly) -> hw\n\
\n\
Computes Hamming weight of the polynomial evaluated on the basis");

static PyMethodDef
        tbase_methods[] = {
        {"eval_poly_hw", (PyCFunction) base_eval_polynomial_hw, METH_VARARGS, base_eval_polynomial_hw_doc},
        {NULL,           NULL}  /* sentinel */
};

static PyTypeObject TBase_Type = {
#ifdef IS_PY3K
        PyVarObject_HEAD_INIT(NULL, 0)
#else
        PyObject_HEAD_INIT(NULL)
        0,                                           /* ob_size */
#endif
        "bitarray.tbase",                          /* tp_name */
        sizeof(tbase),                                       /* tp_basicsize */
        0,                                       /* tp_itemsize */
        /* methods */
        (destructor) tbase_dealloc,                          /* tp_dealloc */
        0,                                          /* tp_print */
        0,                                          /* tp_getattr */
        0,                                         /* tp_setattr */
        0,                                       /* tp_compare */
        0,                                        /* tp_repr */
        0,                                        /* tp_as_number */
        0,                                        /* tp_as_sequence */
        0,                                        /* tp_as_mapping */
        0,                                        /* tp_hash */
        0,                                        /* tp_call */
        0,                                        /* tp_str */
        PyObject_GenericGetAttr,                  /* tp_getattro */
        0,                                        /* tp_setattro */
        0,                                        /* tp_as_buffer */
        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,  /* tp_flags */
        0,                                        /* tp_doc */
        (traverseproc)tbase_traverse,                     /* tp_traverse */
        0,                                        /* tp_clear  - empty for immutable */
        0,                                  /* tp_richcompare */
        0,                                /* tp_weaklistoffset */
        0,                                        /* tp_iter */
        0,                                        /* tp_iternext */
        tbase_methods,                            /* tp_methods */
        0,                                        /* tp_members */
        0,                                        /* tp_getset */
        0,                                        /* tp_base */
        0,                                        /* tp_dict */
        0,                                        /* tp_descr_get */
        0,                                        /* tp_descr_set */
        0,                                        /* tp_dictoffset */
        0,                                        /* tp_init */
        PyType_GenericAlloc,                     /* tp_alloc */
        tbase_new,                                /* tp_new */
        PyObject_GC_Del
};

