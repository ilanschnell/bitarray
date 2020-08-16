#include <stdio.h>
#include <stdlib.h>


typedef struct {
    int size;
    int nbits;
    int allocated;
} bitarrayobject;


/* number of bytes necessary to store given bits */
#define BYTES(bits)  (((bits) == 0) ? 0 : (((bits) - 1) / 8 + 1))


int resize(bitarrayobject *self, int nbits)
{
    int new_allocated, allocated = self->allocated, size = self->size;
    int newsize;

    newsize = BYTES(nbits);
    if (newsize == size) {
        /* the memory size hasn't changed - bypass almost everything */
        self->nbits = nbits;
        return 0;
    }

    /* Bypass realloc() ... */
    if (allocated >= newsize && newsize >= (allocated >> 1)) {
        self->size = newsize;
        self->nbits = nbits;
        return 0;
    }

    if (newsize == 0) {
        /* free(self->ob_item) */
        self->size = 0;
        self->allocated = 0;
        self->nbits = 0;
        return 0;
    }

    new_allocated = newsize;
    if (size == 0 && newsize <= 4)
        /* When resizing an empty bitarray, we want at least 4 bytes. */
        new_allocated = 4;

    else if (size != 0 && newsize > size)
        new_allocated += (newsize >> 4) + (newsize < 8 ? 3 : 7);

    /* realloc(self->ob_item) */
    self->size = newsize;
    self->allocated = new_allocated;
    self->nbits = 0;
    return 1;
}


int main()
{
    int size;
    bitarrayobject x;

    x.size = 0;
    x.allocated = 0;

#define SHOW  printf("%d  %d\n", x.size, x.allocated)

    resize(&x, 0);      SHOW;
    for (size = 0; size < 2000; size++)
        if (resize(&x, size))
            SHOW;

    resize(&x, 800000);  SHOW;
    resize(&x, 400000);  SHOW;
    resize(&x, 399992);  SHOW;
    resize(&x, 0);       SHOW;
    resize(&x, 0);       SHOW;
    resize(&x, 80000);   SHOW;
    return 0;
}
