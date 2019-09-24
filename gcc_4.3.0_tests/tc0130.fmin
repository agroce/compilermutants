int printf (const char *, ...);
union
{
    int f0;
    int f4;
}
a, b;
void
fn1 ()
{
    b.f4 = 1;
    a = b;
    a = a;
}

int
main ()
{
    fn1 ();
    printf ("%d\n", a.f0);
    return 0;
}
