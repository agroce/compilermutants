int printf (const char *, ...);
int *a;
int b;
int
fn1 (int p1, int p2)
{
    return p1 - p2;
}

int
main ()
{
    int **c = &a, **d = &a;
    b = fn1 (c == d, 10);
    printf ("%d\n", b);
    return 0;
}
