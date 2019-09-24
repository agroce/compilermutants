int printf (const char *, ...);
int a = 1, d = 1;
int *const volatile b = &a;
char c;
char *e = &c;
void fn1 ();
int
fn2 ()
{
    for (;;)
    {
        fn1 ();
        if (d)
            return 0;
    }
}

void
fn1 ()
{
    int *f = &a;
    *e = *f = 0;
    *f = *b;
}

int
main ()
{
    fn2 ();
    printf ("%d\n", a);
    return 0;
}
