int printf (const char *, ...);
struct S0
{
    int f0;
    int f7;
}
a, *b;
void
fn1 (struct S0 *p1)
{
    b = p1;
}

int
main ()
{
    struct S0 c = { };
    struct S0 *d = &c;
    a = *d = c;
    fn1 (&c);
    printf ("%d\n", a.f0);
    return 0;
}
