int printf (const char *, ...);
int a = 1;
static const int *b = &a;
static int *
fn1 (int *p1)
{
    a = 0;
    *p1 = *b;
    return 0;
}

int
main ()
{
    fn1 (&a);
    printf ("%d\n", a);
    return 0;
}
