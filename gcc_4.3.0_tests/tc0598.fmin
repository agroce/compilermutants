int printf (const char *, ...);
struct S0
{
    int f1;
    long f2;
    int f3;
    int f4;
}
a;
int
main ()
{
    struct S0 b = { 0, 0, 0, 1 };
    struct S0 *c = &a, *d = &a;
    *c = b;
    *d = a;
    printf ("%d\n", a.f4);
    return 0;
}
