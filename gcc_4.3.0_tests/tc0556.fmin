int printf (const char *, ...);
int a, b;
int *c = &b;
int
main ()
{
    int *d = &a;
    *c = d == &a;
    *c ^= 6;
    printf ("%d\n", b);
    return 0;
}
