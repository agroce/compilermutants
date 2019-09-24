int printf (const char *, ...);
struct
{
    int f0;
    int f6;
}
a, b =
{
    1
};

void
fn1 ()
{
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
