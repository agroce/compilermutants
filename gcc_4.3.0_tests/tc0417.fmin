int printf (const char *, ...);
int a, b;
int *c = &b;
int
main ()
{
    int *d = &a, *e = &a;
    *c = e == d;
    *c ^= 10;
    printf ("%d\n", b);
    return 0;
}
