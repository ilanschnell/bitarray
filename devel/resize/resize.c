#include <stdio.h>
#include <stdlib.h>


typedef struct {
    size_t size;
    size_t nbits;
    size_t allocated;
} bitarrayobject;


/* number of bytes necessary to store given bits */
#define BYTES(bits)  (((bits) + 7) >> 3)

uint64_t s = 290797;

int bbs(void)
{
    s *= s;
    s %= 50515093;
    return s % 8000;
}

size_t new_allocation(size_t size, size_t allocated, size_t newsize)
{
    if (allocated >= newsize) {
        /* current buffer is large enough to host the requested size */
        if (newsize >= allocated / 2)
            return allocated;  /* minor downsize - keep current allocation */

        return newsize;  /* major downsize - shrink to exact size */
    }
    else {
          /* need to grow buffer */
          size_t new_alloc = newsize;
          /* overallocate when previous size isn't zero and when growth
             is moderate */
          if (size != 0 && newsize / 2 <= allocated) {
              /* overallocate proportional to the bitarray size and
                 add padding to make the allocated size multiple of 4 */
              new_alloc += (newsize >> 4) + (newsize < 8 ? 3 : 7);
              new_alloc &= ~(size_t) 3;
          }
          return new_alloc;
    }
}

void resize(bitarrayobject *self, size_t nbits)
{
    size_t size = self->size, allocated = self->allocated;
    size_t newsize = BYTES(nbits), new_allocated;

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

    new_allocated = new_allocation(size, allocated, newsize);

    if (new_allocated == allocated) {
        /* bypass reallocation */
        self->size = newsize;
        self->nbits = nbits;
        return;
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

#define SHOW  printf("%lu  %lu\n", x.size, x.allocated)

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
    resize(&x, 500000);  SHOW;
    resize(&x, 0);       SHOW;
    resize(&x, 0);       SHOW;
    resize(&x, 10000);   SHOW;
    resize(&x,   400);   SHOW;
    resize(&x,   600);   SHOW;
    resize(&x,  2000);   SHOW;

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
        resize(&x, bbs());
        SHOW;
    }

    return 0;
}
