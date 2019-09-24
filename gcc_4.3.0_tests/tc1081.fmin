int printf (const char *, ...);
short a;
int b = 1;
static const int *c = &b;
short *d = &a;
int
main ()
{
    int *e = &b;
    *e = 0;
    if (*c)
        *d = 1;
    *e = 0;
    printf ("%d\n", a);
    return 0;
}
