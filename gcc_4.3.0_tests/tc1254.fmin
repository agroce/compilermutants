int printf (const char *, ...);
struct S0
{
    int f4;
    int f6;
    int f7;
}
a, b =
{
    0, 0, 1
};

int
main ()
{
    struct S0 *c = &b;
    *c = a;
    *c = *c;
    printf ("%d\n", b.f7);
    return 0;
}
