int printf (const char *, ...);
int a;
int *b = &a;
int
fn1 (p1)
{
    return 1 + p1;
}

int
main ()
{
    int **c = &b;
    *b = fn1 (c == &b);
    printf ("%d\n", a);
    return 0;
}
