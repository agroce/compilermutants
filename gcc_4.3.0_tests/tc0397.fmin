int printf (const char *, ...);
int a = 1, d;
int *b = &a;
static int *const *c = &b;
int
main ()
{
    if (*b)
    {
        int **e = &b, **f = &b;
        *e = &d;
        **c = 0;
        *f = &d;
    }
    **c = 0;
    printf ("%d\n", a);
    return 0;
}
