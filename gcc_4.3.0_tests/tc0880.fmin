int printf (const char *, ...);
int a, b;
static const int *c = &a;
int *d = &b;
int
main ()
{
    int *e = &a, *f = &a;
    *f = 1;
    *d = *c;
    *e = 0;
    printf ("%d\n", b);
    return 0;
}
