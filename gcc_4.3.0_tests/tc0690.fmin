int printf (const char *, ...);
int a;
static int b;
int c[4];
static int *d = &c[3], *e, *f = &a;
int **g = &e;
int
main ()
{
    int *h = &c[3];
    *g = f;
    b = 0;
    for (; b <= 1; b++)
    {
        *e = *d;
        *h = 1;
    }
    printf ("%d\n", a);
    return 0;
}
