int printf (const char *, ...);
struct S0
{
    int f2;
    int f3:1;
    int f4;
    int f5;
}
a, d =
{
    0, 1
}

, *const b = &a;
static const struct S0 *c = &a;
int
main ()
{
    struct S0 e;
    *b = d;
    e = *c;
    *b = e;
    printf ("%d\n", a.f3);
    return 0;
}
