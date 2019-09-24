int printf (const char *, ...);
struct S2
{
    int f3;
    int f4;
    int f5;
    int f6;
}
d;
struct S8
{
    int f6;
    struct S2 f7;
};
int a, c;
int *b = &a;
void
fn1 ()
{
    struct S2 e;
    struct S8 f = { };
    struct S8 *g = &f;
    *g = f;
    e = f.f7;
    d = e;
    struct S2 h = d;
    c = h.f6;
    *b = c;
}

int
main ()
{
    fn1 ();
    printf ("%d\n", a);
    return 0;
}
