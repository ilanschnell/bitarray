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
        self->nbits = nbits;
        return;
    }

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

    new_allocated = (newsize + (newsize >> 4) + (newsize < 8 ? 3 : 7)) & ~3;

#if 0
    printf("size = %d  newsize = %d  allocated = %d  new_allocated = %d\n",
           size, newsize, allocated, new_allocated);
#endif
    if (newsize - size > new_allocated - newsize)
        new_allocated = (newsize + 3) & ~(int) 3;

    /* realloc(self->ob_item) */
    self->size = newsize;
    self->allocated = new_allocated;
    self->nbits = nbits;
}


int main()
{
    int nbits, prev_alloc = -1;
    bitarrayobject x;

    x.size = 0;
    x.allocated = 0;

#define SHOW  printf("%d  %d\n", x.size, x.allocated)

    for (nbits = 0; nbits < 2000; nbits++) {
        if (prev_alloc != x.allocated)
            SHOW;
        prev_alloc = x.allocated;
        resize(&x, nbits);
    }

    resize(&x, 800000);  SHOW;
    resize(&x, 400000);  SHOW;
    resize(&x, 399992);  SHOW;
    resize(&x, 0);       SHOW;
    resize(&x, 0);       SHOW;
    resize(&x, 80000);   SHOW;
    resize(&x, 2000);    SHOW;

    for (nbits = 2000; nbits >= 0; nbits--) {
        if (prev_alloc != x.allocated)
            SHOW;
        prev_alloc = x.allocated;
        resize(&x, nbits);
    }
    SHOW;

    return 0;
}
