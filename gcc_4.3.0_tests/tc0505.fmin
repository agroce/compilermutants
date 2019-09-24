int printf (const char *, ...);
int a, d;
struct
{
    int f3;
    int f6;
}
b, c =
{
    0, 1
};

void fn1 ();
void
fn2 ()
{
    fn1 ();
    c = b;
    c = c;
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
    printf ("%d\n", c.f6);
    return 0;
}
