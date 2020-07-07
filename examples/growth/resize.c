#include <stdio.h>
#include <stdlib.h>


typedef struct {
    int size;
    int allocated;
} obj_t;

int resize(obj_t *self, int newsize)
{
    int new_allocated, allocated = self->allocated;

    if (allocated >= newsize && newsize >= (allocated >> 1)) {
        self->size = newsize;
        return 0;
    }
    new_allocated = newsize;
    if (newsize < self->size + 65536)
        new_allocated += (newsize >> 4) + (newsize < 8 ? 3 : 7);

    if (newsize == 0)
        new_allocated = 0;

    self->size = newsize;
    self->allocated = new_allocated;
    return 1;
}

int main()
{
    int size;
    obj_t x;

    x.size = 0;
    x.allocated = 0;

#define SHOW  printf("%d  %d\n", x.size, x.allocated)

    resize(&x, 0);      SHOW;
    for (size = 0; size < 200; size++)
        if (resize(&x, size))
            SHOW;

    resize(&x, 100000); SHOW;
    resize(&x, 50000);  SHOW;
    resize(&x, 49999);  SHOW;
    resize(&x, 0);      SHOW;
    resize(&x, 0);      SHOW;
    resize(&x, 10000);  SHOW;
    return 0;
}
