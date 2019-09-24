int printf (const char *, ...);
int a, f;
int *b = &a;
static int *const *c = &b;
void
fn1 (int *p1)
{
    int **d = &b, **e = &b;
    *d = p1;
    *e = *c;
}

int
main ()
{
    fn1 (&f);
    **c = 1;
    printf ("%d\n", a);
    return 0;
}
