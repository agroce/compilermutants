int printf (const char *, ...);
int a = 1, c, e;
static const int *b = &a;
int *d = &c;
int
fn1 ()
{
    return *b;
}

int
main ()
{
    int *f = &a;
    *f = 0;
    *f = fn1 ();
    e = *b;
    *d = e;
    printf ("%d\n", c);
    return 0;
}
