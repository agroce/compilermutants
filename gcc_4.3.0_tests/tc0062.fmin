int printf (const char *, ...);
int a, b, c, f;
static const int *d = &c;
static int *e = &c;
void fn1 ();
void
fn2 ()
{
    for (; c <= 0; c++)
        fn1 ();
}

void
fn1 ()
{
    for (; b; b--)
        f = a || 1 >> a ? 0 : 1;
}

int
main ()
{
    fn2 ();
    *e = *d;
    printf ("%d\n", c);
    return 0;
}
