int printf (const char *, ...);
struct S1
{
    int f3;
    int f4;
    int f5;
}
a, b =
{
    0, 1
}

, *d = &b, *e = &a;
int c;
int
main ()
{
    c = 0;
    for (; c <= 1; c++)
    {
        struct S1 *f = &a;
        *f = *d;
        d = e;
    }
    printf ("%d\n", a.f4);
    return 0;
}
