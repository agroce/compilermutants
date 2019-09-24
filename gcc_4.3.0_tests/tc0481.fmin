int printf (const char *, ...);
int *a;
static int **b = &a;
int c[7][16];
int d = 1, e, g, h;
int *
fn1 ()
{
    if (d != 1)
    {
        int *f = &c[6][15];
        return f;
    }
    return &e;
}

int
main ()
{
    int i = 0;
    if (h)
        i = 1;
    int *j = &c[6][15];
    *b = fn1 ();
    *b = 0;
    *j = 1;
    g = 0;
    for (; g < 1; g++)
        if (i)
            printf ("index = [%d]\n", 0);
    printf ("%d\n", e);
    return 0;
}
