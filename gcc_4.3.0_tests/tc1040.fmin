int printf (const char *, ...);
int a = 1;
static const int *b = &a;
short c = 1;
short *d = &c;
int **
fn1 ()
{
    int *e = &a;
    *e = 0;
    *e = *b;
    if (*b)
        return 0;
    *d = 0;
    return 0;
}

int
main ()
{
    fn1 ();
    printf ("%d\n", c);
    return 0;
}
