int printf (const char *, ...);
int a = 1, d, e;
int *b = &a, *g = &a, *h = &e;
static int *const *c = &b;
void
fn1 ()
{
    int **f = &b;
    *f = 0;
}

void
fn2 (int **p1)
{
    *h = **p1;
}

int
main ()
{
    int **i = &b;
    *i = &d;
    **c = 0;
    fn1 ();
    fn2 (&g);
    printf ("%d\n", e);
    return 0;
}
