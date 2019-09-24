int printf (const char *, ...);
int *a;
static int **b = &a, **e;
int c;
int *d = &c;
int
fn1 (p1)
{
    return 1 + p1;
}

int
main ()
{
    e = &a;
    *d = fn1 (e == b);
    printf ("%d\n", c);
    return 0;
}
