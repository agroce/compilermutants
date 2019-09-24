int printf (const char *, ...);
int a = 1, b = 1;
static const int *c = &a;
static int *d = &a;
int
main ()
{
    int *e = &a;
    *e = 0;
    if (*c)
        b = 0;
    *d = 0;
    printf ("%d\n", b);
    return 0;
}
