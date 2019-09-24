int printf (const char *, ...);
int a = 1, b;
static const int *c = &a;
int
main ()
{
    int *d = &a;
    a = 0;
    b = *c;
    *d = 0;
    printf ("%d\n", b);
    return 0;
}
