int printf (const char *, ...);
int *a;
static int **b = &a, **c = &a;
int d;
int *e = &d;
void
fn1 (int p1, int *p2)
{
    *p2 = p1 ^ 3;
}

int
main ()
{
    fn1 (b != c, e);
    printf ("%d\n", d);
    return 0;
}
