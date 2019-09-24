int printf (const char *, ...);
struct S0
{
    int f2;
    int f3;
    int f5;
}
a, b =
{
    0, 0, 1
};

int
main ()
{
    struct S0 *c = &a;
    *c = b;
    a = *c;
    printf ("%d\n", a.f5);
    return 0;
}
