int printf (const char *, ...);
int a, e;
int *b;
static const int *c = &a;
int **d = &b;
int *
fn1 ()
{
    return &e;
}

int
main ()
{
    int *f = &a;
    *f = 1;
    *d = fn1 ();
    *f = *c;
    printf ("%d\n", a);
    return 0;
}
