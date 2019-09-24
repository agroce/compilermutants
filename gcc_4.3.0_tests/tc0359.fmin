int printf (const char *, ...);
int a[5];
static int *b = &a[4];
int c, d;
int *e = &c;
int
main ()
{
    int *f = &a[4];
    *f = 1;
    d = *b;
    *e = d;
    *f = 0;
    printf ("%d\n", c);
    return 0;
}
