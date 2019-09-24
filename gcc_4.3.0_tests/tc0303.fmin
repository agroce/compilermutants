int printf (const char *, ...);
int a, d = 1;
int *b = &a;
static int *const *c = &b;
void
fn1 ()
{
    int **e = &b;
    *e = &d;
    **c = 0;
}

int
main ()
{
    int **f = &b;
    fn1 ();
    *f = 0;
    printf ("%d\n", d);
    return 0;
}
