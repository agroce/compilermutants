int printf (const char *, ...);
union
{
    int f0;
    int f3;
    short f4;
}
b, d;
short a;
short *c = &b.f4;
int
main ()
{
    short *e = &a;
    int f = 5L;
    f |= e == &a;
    d.f3 = f;
    *c = d.f0;
    printf ("%d\n", b.f3);
    return 0;
}
