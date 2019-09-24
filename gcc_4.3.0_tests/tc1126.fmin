int printf (const char *, ...);
char a;
int b;
int *c, *d = &b;
int *fn1 ();
void
fn2 (p1)
{
    c = fn1 ();
    *c = p1;
}

int *
fn1 ()
{
    return d;
}

int
main ()
{
    char *e = &a;
    int f = 4;
    fn2 (f | e != &a);
    printf ("%d\n", b);
    return 0;
}
