int printf (const char *, ...);
int a = 1, b;
int *c = &b;
static const int *d = &a;
int
main ()
{
    int *e = &a;
    *e = 0;
    *c = *d;
    a = 0;
    printf ("%d\n", b);
    return 0;
}
