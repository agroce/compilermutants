int printf (const char *, ...);
int a;
int *b, *d = &a;
static int **c = &b;
int
fn1 (p1)
{
    return 1 + p1;
}

int
main ()
{
    *d = fn1 (c == &b);
    printf ("%d\n", a);
    return 0;
}
