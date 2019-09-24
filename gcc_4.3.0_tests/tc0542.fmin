int printf (const char *, ...);
int a, b = 1, f, g, h;
static const int *c = &b;
static int *d = &b;
void fn1 ();
int *
fn2 ()
{
    int *e = &b;
    *e = 0;
    fn1 ();
    return 0;
}

void
fn1 ()
{
    g = a || h && a < 0 < a ? 0 : 1 + a;
    f = 1 | 1 ^ g;
}

int
main ()
{
    *fn2 ();
    *d = *c;
    printf ("%d\n", b);
    return 0;
}
