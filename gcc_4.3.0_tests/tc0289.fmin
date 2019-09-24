int printf (const char *, ...);
struct
{
    int f5;
    int f6;
}
a, b =
{
    0, 1
};

void
fn1 ()
{
    b = a;
    b = b;
}

int
main ()
{
    fn1 ();
    printf ("%d\n", b.f6);
    return 0;
}
