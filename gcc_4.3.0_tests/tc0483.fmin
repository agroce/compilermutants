int printf (const char *, ...);
struct S0
{
    int f5;
};
struct S2
{
    int f0;
    struct S0 f1;
    int f2;
    struct S0 f3;
    int f8;
}
b, *c = &b;
int a;
int *d, *e = &a, *g = &b.f1.f5;
int **f = &d;
int
main ()
{
    *f = e;
    struct S2 h = { 0, 1 };
    *c = h;
    *c = *c;
    *d = *g;
    printf ("%d\n", a);
    return 0;
}
