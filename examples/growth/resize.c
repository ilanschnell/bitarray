#include <stdio.h>
#include <stdlib.h>


typedef struct {
    int size;
    int nbits;
    int allocated;
} bitarrayobject;


/* number of bytes necessary to store given bits */
#define BYTES(bits)  (((bits) + 7) >> 3)

uint64_t s = 290797;

int bbs(void)
{
    s *= s;
    s %= 50515093;
    return s % 1000;
}

void resize(bitarrayobject *self, int nbits)
{
    int new_allocated, allocated = self->allocated, size = self->size;
    int newsize;

    newsize = BYTES(nbits);
    if (newsize == size) {
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

    if (allocated >= newsize) {
        if (newsize >= allocated / 2) {
            self->size = newsize;
            self->nbits = nbits;
            return;
        }
        new_allocated = newsize;
    }
    else {
        new_allocated = newsize;
        if (size != 0 && newsize / 2 <= allocated) {
            new_allocated += (newsize >> 4) + (newsize < 8 ? 3 : 7);
            new_allocated &= ~3;
        }
    }

    /* realloc(self->ob_item) */
    self->size = newsize;
    self->allocated = new_allocated;
    self->nbits = nbits;
}


int main()
{
    int i, nbits, prev_alloc = -1;
    bitarrayobject x;

#define SHOW  printf("%d  %d\n", x.size, x.allocated)

    x.size = 0;
    x.allocated = 0;
    for (nbits = 0; nbits < 1000; nbits++) {
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

    for (nbits = 0; nbits < 100; nbits += 8) {
        x.size = 0;
        x.allocated = 0;
        resize(&x, nbits);
        SHOW;
    }

    for (i = 0; i < 100000; i++) {
        nbits = 8 * bbs();
        resize(&x, nbits);
        SHOW;
    }

    return 0;
}
