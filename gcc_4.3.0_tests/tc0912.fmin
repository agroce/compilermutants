int printf (const char *, ...);
struct S1
{
    int f0;
    int f1;
    int f2;
}
a =
{
    1
}

, c, *b = &a;
struct S1 *
fn1 ()
{
    *b = *b;
    return 0;
}

void
fn2 (struct S1 *p1)
{
    *b = *p1;
}

int
main ()
{
    fn2 (&c);
    *fn1 ();
    printf ("%d\n", a.f0);
    return 0;
}
