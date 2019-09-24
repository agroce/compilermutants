int printf (const char *, ...);
int a, d, f;
int *b = &a, *e = &d, *g = &a;
int **c = &b;
int
fn1 (int p1, int p2)
{
    return p1 - p2;
}

int
main ()
{
    int ***h = &c, ***i = &c;
    f = fn1 (h == i, 4);
    ***h = f;
    *e = *g;
    printf ("%d\n", d);
    return 0;
}
