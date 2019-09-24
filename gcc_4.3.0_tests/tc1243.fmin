int printf (const char *, ...);
int a = 1, g, i;
short b;
int *const c = &a, *h = &a;
int *const d = &a;
static const int *e = &a;
short *f = &b;
int
fn1 (const int **p1)
{
    i = **p1;
    *c = i;
    return *h;
}

int
main ()
{
    *d = 0;
    g = fn1 (&e);
    *f = g;
    printf ("%d\n", b);
    return 0;
}
