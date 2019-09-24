int printf (const char *, ...);
int a, c;
int *b = &a, *d = &c, *e = &a;
int
fn1 ()
{
    if (*e)
        return 1;
    return 0;
}

int
main ()
{
    int **f = &b;
    **f = &b != f;
    *b = **f + (int) 2L;
    *d = fn1 ();
    printf ("%d\n", c);
    return 0;
}
