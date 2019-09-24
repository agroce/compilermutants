int printf (const char *, ...);
struct S0
{
    int f3;
};
struct S1
{
    int f0;
    struct S0 f1;
    int f4;
};
int a;
int *b = &a;
long c;
int
fn1 (int p1, int p2)
{
    return p2 ? p1 : 0;
}

void
fn2 (p1)
{
    struct S1 d = { };
    struct S1 *e = &d;
    *e = d;
    if (fn1 (d.f1.f3, c || p1))
    {
        struct S1 **f = &e;
        *b = 1;
    }
}

int
main ()
{
    fn2 (1);
    printf ("%d\n", a);
    return 0;
}
