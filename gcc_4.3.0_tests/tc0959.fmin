int printf (const char *, ...);
int a = 1, b;
static const int *c = &a;
int
fn1 ()
{
    b = 1;
    for (; b >= 0; b--)
        if (*c)
            a = 0;
        else
        {
            a = 0;
            return 0;
        }
    return 0;
}

int
main ()
{
    fn1 ();
    printf ("%d\n", b);
    return 0;
}
