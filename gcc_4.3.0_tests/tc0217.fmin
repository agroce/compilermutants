int printf (const char *, ...);
struct S0
{
    int f3;
    int f6;
    int f8;
};
struct S1
{
    int f0;
    int f1;
    int f2;
    int f3;
    int f4;
};
struct S2
{
    int f0;
    int f1;
    struct S1 f2;
    int f3;
    int f4;
    int f5;
    int f6;
}
d;
struct S6
{
    int f0;
    struct S2 f1;
    int f2;
    int f3;
    int f4;
    int f6;
    struct S0 f8;
}
b, e;
int a, c = 1, g;
void fn1 (struct S6);
int *
fn2 ()
{
    int *f = &b.f1.f4;
    fn1 (e);
    return f;
}

void
fn1 (struct S6 p1)
{
    for (; a;)
        g = p1.f8.f6;
}

int
main ()
{
    int *h = &d.f4;
    int **i = &h;
    if (c)
        *i = fn2 ();
    else
        *h;
    *h = 1;
    printf ("%d\n", d.f4);
    return 0;
}
