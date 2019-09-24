int printf (const char *, ...);
struct S0
{
    int f1;
    int f6;
    int f7;
    int f8;
    int f9;
}
a, b, *c = &a, *d = &b;
int
main ()
{
    struct S0 e = { 1 };
    *d = e;
    *d = *d;
    *c = b;
    printf ("%d\n", a.f1);
    return 0;
}
