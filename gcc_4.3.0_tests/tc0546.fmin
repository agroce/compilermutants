int printf (const char *, ...);
int a;
static const int *b = &a;
static int *c = &a;
int
fn1 ()
{
    return 0;
}

int
main ()
{
    a = 1;
    if (fn1 ())
        b = 0;
    *c = *b;
    printf ("%d\n", a);
    return 0;
}
