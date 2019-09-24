int printf (const char *, ...);
int a, b, c;
int *d = &c, *e = &c, *f = &a;
int
main ()
{
    int *g = &b;
    *e = -(g == &b);
    int h = *d;
    *f = h;
    printf ("%d\n", a);
    return 0;
}
