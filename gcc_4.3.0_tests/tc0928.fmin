int printf (const char *, ...);
int a = 1, b;
static const int *c = &a;
int *d = &b;
int
main ()
{
    int *e = &a, *f = &a;
    *e = 0;
    if (*c)
        *d = 1;
    *f = 0;
    printf ("%d\n", b);
    return 0;
}
