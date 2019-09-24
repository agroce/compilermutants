int printf (const char *, ...);
char **a;
int
fn1 (p1)
{
    return 1 - p1;
}

int
fn2 ()
{
    char ***b = &a;
    if (b != &a | fn1 (5UL))
        return 1;
    return 0;
}

int
main ()
{
    if (fn2 ())
        printf ("%d\n", 0);
    return 0;
}
