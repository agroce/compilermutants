int printf (const char *, ...);
int a;
int *b;
int **c;
int
fn1 (p1)
{
    return p1 + 1;
}

int
main ()
{
    a = fn1 ((c = &b) == &b);
    printf ("%d\n", a);
    return 0;
}
