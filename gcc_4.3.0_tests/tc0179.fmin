int printf (const char *, ...);
int a, b;
int *c = &b;
static int *const *d = &c;
void
fn1 ()
{
    c = &a;
}

int
main ()
{
    fn1 ();
    int **e = &c;
    **d = 1;
    *e = 0;
    printf ("%d\n", b);
    return 0;
}
