int printf (const char *, ...);
int a[7][2] = { {}, {}, {}, {}, {}, 0, -1 };

int *b = &a[5][1];
static int *const *c = &b;
static int **d = &b;
int e, f;
int
main ()
{
    *d = &e;
    **c &= 1;
    *d = 0;
    for (; f < 7; f++)
        printf ("%d\n", a[f][1]);
    return 0;
}
