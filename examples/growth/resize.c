#include <stdio.h>
#include <stdlib.h>


typedef struct {
    int size;
    int nbits;
    int allocated;
} bitarrayobject;


/* number of bytes necessary to store given bits */
#define BYTES(bits)  (((bits) + 7) >> 3)


void resize(bitarrayobject *self, int nbits)
{
    int new_allocated, allocated = self->allocated, size = self->size;
    int newsize;

    newsize = BYTES(nbits);
    if (newsize == size) {
        /* the memory size hasn't changed - bypass almost everything */
        self->nbits = nbits;
        return;
    }

    /* Bypass realloc() ... */
    if (allocated >= newsize && newsize >= (allocated >> 1)) {
        self->size = newsize;
        self->nbits = nbits;
        return;
    }

    if (newsize == 0) {
        /* free(self->ob_item) */
        self->size = 0;
        self->allocated = 0;
        self->nbits = 0;
        return;
    }

    new_allocated = (newsize + (newsize >> 4) +
                     (newsize < 8 ? 3 : 7)) & ~(int) 3;

    if (newsize - size > new_allocated - newsize)
        new_allocated = (newsize + 3) & ~(int) 3;

    /* realloc(self->ob_item) */
    self->size = newsize;
    self->allocated = new_allocated;
    self->nbits = nbits;
}


int main()
{
    int size, prev_alloc = -1;
    bitarrayobject x;

    x.size = 0;
    x.allocated = 0;

#define SHOW  printf("%d  %d\n", x.size, x.allocated)

    for (size = 0; size < 2000; size++) {
        if (prev_alloc != x.allocated)
            SHOW;
        prev_alloc = x.allocated;
        resize(&x, size);
    }

    resize(&x, 800000);  SHOW;
    resize(&x, 400000);  SHOW;
    resize(&x, 399992);  SHOW;
    resize(&x, 0);       SHOW;
    resize(&x, 0);       SHOW;
    resize(&x, 80000);   SHOW;
    return 0;
}
