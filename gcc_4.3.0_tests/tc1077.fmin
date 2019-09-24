int printf (const char *, ...);
int a, d;
int **b;
static int ***c = &b;
int
fn1 (int p1, int p2)
{
    return p1 + p2;
}

void
fn2 (int *p1)
{
    int ***e = &b;
    d = fn1 (3, c != e);
    *p1 = d;
}

int
main ()
{
    fn2 (&a);
    printf ("%d\n", a);
    return 0;
}
