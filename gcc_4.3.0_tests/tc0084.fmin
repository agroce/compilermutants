int printf (const char *, ...);
int a, b, c, d, i, j;
static const int *e = &d;
int *f, *g, *k = &c;
int
fn1 ()
{
    return a ? 0 : b < 0;
}

void fn2 ();
void
fn3 ()
{
    int **h = &f;
    fn2 ();
    g = &d;
    *h = g;
    *f = 1;
}

void
fn2 ()
{
    j |= i || fn1 ();
}

int
main ()
{
    fn3 ();
    *k = *e;
    int *l = &d;
    *l = 0;
    printf ("%d\n", c);
    return 0;
}
