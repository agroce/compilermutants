int printf (const char *, ...);
int a = 1, d;
int *b = &a;
static int *const *c = &b;
void
fn1 (int *p1)
{
    b = p1;
}

int
main ()
{
    fn1 (&d);
    **c = 0;
    b = 0;
    printf ("%d\n", a);
    return 0;
}
