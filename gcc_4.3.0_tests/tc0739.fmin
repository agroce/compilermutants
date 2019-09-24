int printf (const char *, ...);
int a = 1, b;
static const int *c = &a;
int
main ()
{
    int *d = &a, *e = &a;
    *e = 0;
    if (*c)
        b = 1;
    *d = 0;
    printf ("%d\n", b);
    return 0;
}
