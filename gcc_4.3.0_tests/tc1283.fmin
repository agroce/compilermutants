int printf (const char *, ...);
int a, b;
int *const c = &a, *d = &b, *e = &a;
unsigned *f;
int
main ()
{
    unsigned **g = &f;
    *c = -1L;
    *c |= g == &f;
    *d = *e;
    printf ("%d\n", b);
    return 0;
}
