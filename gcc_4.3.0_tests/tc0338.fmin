int printf (const char *, ...);
int a, b;
short c;
short *f = &c;
void
fn1 ()
{
    b && a && 0 / 0 && a && 0 / 0;
}

void fn2 ();
void
fn3 ()
{
    {
        int d = 0;
        fn2 (&d);
        d = 0;
        fn1 ();
    }
    {
        int e = 1;
        fn2 (&e);
    }
}

void
fn2 (int *p1)
{
    *f = *p1;
}

int
main ()
{
    fn3 ();
    printf ("%d\n", c);
    return 0;
}
