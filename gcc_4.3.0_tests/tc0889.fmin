int printf (const char *, ...);
int a;
int *b = &a;
int **const *c;
int
fn1 (int p1, int p2)
{
    return p1 - p2;
}

int
main ()
{
    int **const **d = &c;
    *b = fn1 (d == &c, 10);
    printf ("%d\n", a);
    return 0;
}
