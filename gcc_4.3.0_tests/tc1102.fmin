int printf (const char *, ...);
int a = 1, e, f;
static int b[] = { 0 };

unsigned char c;
void fn1 ();
int
fn2 ()
{
    int d = 0;
    for (;;)
    {
        fn1 ();
        if (c >= b[0])
            for (; d <= 1; d++)
                fn1 ();
        else
            continue;
        return 0;
    }
}

void
fn1 ()
{
    e = 0;
    for (; e <= 0; e = e + 1)
        f = a < 0 ? a : a << 1;
}

int
main ()
{
    fn2 ();
    printf ("%d\n", f);
    return 0;
}
