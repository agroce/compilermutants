int printf (const char *, ...);
struct S0
{
    int f5;
};
struct S1
{
    struct S0 f2;
    int f3;
}
a, *b, *c = &a;
int
main ()
{
    struct S1 d = { };
    struct S1 *e = &d;
    *c = *e = d;
    b = &d;
    printf ("%d\n", a.f2.f5);
    return 0;
}
