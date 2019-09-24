int printf (const char *, ...);
struct S1
{
    int f1:1;
    int f2:1;
}
a, b;
void
fn1 ()
{
    struct S1 *c = &a, *e = &a;
    struct S1 d = { 0, 1 };
    b = d;
    *c = b;
    *e = a;
}

int
main ()
{
    fn1 ();
    printf ("%d\n", a.f2);
    return 0;
}
