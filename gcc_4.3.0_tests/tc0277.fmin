int printf (const char *, ...);
struct S0
{
    int f2;
    int f3;
    long f4;
    int f5;
}
a;
int
main ()
{
    struct S0 *b = &a, *d = &a;
    struct S0 c = { 0, 0, 0, 1 };
    *d = c;
    *b = a;
    printf ("%d\n", a.f5);
    return 0;
}
