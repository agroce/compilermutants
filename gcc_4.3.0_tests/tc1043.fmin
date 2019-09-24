int printf (const char *, ...);
int a;
int *b = &a;
int
fn1 (int p1, int p2)
{
    return p1 + p2;
}

int
main ()
{
    int **c = &b;
    *b = fn1 (c != &b, 9);
    printf ("%d\n", a);
    return 0;
}
