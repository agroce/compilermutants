int printf (const char *, ...);
int a, e;
int *b = &a, *f = &e, *g;
int *const *const c = &b;
static int *const *const *d = &c;
int
main ()
{
    int **h = &b;
    *h = f;
    g = **d;
    *g = 1;
    *h = 0;
    printf ("%d\n", e);
    return 0;
}
