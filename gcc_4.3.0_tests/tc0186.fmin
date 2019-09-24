int printf (const char *, ...);
int a, b;
static const int *c = &b;
int
main ()
{
    int *d = &b;
    b = 1;
    a = *c;
    *d = 0;
    printf ("%d\n", a);
    return 0;
}
