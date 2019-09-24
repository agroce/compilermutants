int printf (const char *, ...);
int a, b = 1;
unsigned char *c;
int *d = &a, *f = &b, *g = &a;
void fn1 ();
unsigned char fn2 ();
void
fn3 (p1)
{
    unsigned char **e = &c;
    *d = p1 > (&c == e);
    fn1 ();
}

void
fn1 ()
{
    if (fn2 ())
        *f = 0;
}

unsigned char
fn2 ()
{
    return *g;
}

int
main ()
{
    fn3 (8L);
    printf ("%d\n", b);
    return 0;
}
