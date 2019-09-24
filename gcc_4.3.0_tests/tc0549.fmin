int printf (const char *, ...);
struct S0
{
    int f0;
    int f1;
    int f3;
    int f4;
    int f5;
}
b;
int *a;
int
main ()
{
    struct S0 c = { 1, 0, 0, 0, 0 };
    int *d = &c.f1, *e = &c.f1;
    b = c = c;
    a = d;
    *e = 0;
    printf ("%d\n", b.f0);
    return 0;
}
