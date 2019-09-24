int printf (const char *, ...);
int a = 1, c, d;
int *b, *f = &a;
void fn1 ();
void
fn2 (int *p1)
{
    int **e = &b;
    *p1 = 9L ^ (d = e == &b);
    if (*p1)
        fn1 ();
}

void
fn1 ()
{
    *f = 0;
}

int
main ()
{
    fn2 (&c);
    printf ("%d\n", a);
    return 0;
}
