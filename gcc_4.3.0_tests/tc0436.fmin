int printf (const char *, ...);
int a, d;
struct
{
    int f1;
    int f2;
}
b, c =
{
    0, 1
};

void fn1 ();
void
fn2 ()
{
    c = b;
    c = c;
    fn1 ();
}

void
fn1 ()
{
    d = a == 0 ? 0 : 0 % 0;
}

int
main ()
{
    fn2 ();
    printf ("%d\n", c.f2);
    return 0;
}
