int printf (const char *, ...);
union U0
{
    int f2;
    char f3;
}
*d;
int a, b = 1;
static int *c = &b;
static const int *e = &b;
int
main ()
{
    union U0 f = { 1 };
    int g;
    int *h = &b;
    *h = 0;
    g = 0 == f.f3;
    if (g)
    {
        union U0 *i = &f;
        a = 0;
        for (; a;)
            d = i;
    }
    else
        *c = *e;
    printf ("%d\n", b);
    return 0;
}
