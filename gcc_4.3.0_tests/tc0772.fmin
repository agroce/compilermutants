int printf (const char *, ...);
struct S0
{
    int f7;
};
struct S2
{
    int f4;
    struct S0 f6;
}
*a, b, *c = &b;
int
main ()
{
    struct S2 d = b;
    d = d;
    *c = d;
    a = &d;
    printf ("%d\n", b.f6.f7);
    return 0;
}
