int printf (const char *, ...);
int a;
static const int *b = &a;
static short
fn1 (int *p1)
{
    *p1 = 1;
    return 0;
}

int
main ()
{
    int *c = &a;
    fn1 (&a);
    *c = *b;
    printf ("%d\n", a);
    return 0;
}
