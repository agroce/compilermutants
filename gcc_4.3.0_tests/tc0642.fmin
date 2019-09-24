int printf (const char *, ...);
int a;
static int *b = &a, *c = &a;
int
fn1 (int p1, int p2)
{
    return p1 + p2;
}

int
main ()
{
    int **d = &b;
    int *e = *d;
    *c = fn1 (5, e != *d);
    printf ("%d\n", a);
    return 0;
}
