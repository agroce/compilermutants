int printf (const char *, ...);
struct S0
{
    int f0;
    int f1;
    int f5;
    int f6;
    int f7;
}
a;
int b;
void
fn1 ()
{
    struct S0 c = { };
    struct S0 *d = &c;
    struct S0 **e = &d;
    *d = c;
    c.f1 && b;
    a = c;
}

int
main ()
{
    fn1 ();
    printf ("%d\n", a.f0);
    return 0;
}
