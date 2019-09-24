int printf (const char *, ...);
int a, b = 1, c, f;
int *d = &b;
static int *const *e = &d;
int
fn1 ()
{
    return a;
}

int
main ()
{
    int **g = &d;
    *g = &c;
    f = fn1 ();
    **e = 0;
    d = 0;
    printf ("%d\n", b);
    return 0;
}
