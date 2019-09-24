int printf (const char *, ...);
int a = 1, d, f;
int *b = &a, *e = &d;
static int *const *c = &b;
int
main ()
{
    int **g = &b;
    if (**g)
        *g = e;
    **c = 1;
    *g = *g;
    f = **g;
    printf ("%d\n", d);
    return 0;
}
