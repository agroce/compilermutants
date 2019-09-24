int printf (const char *, ...);
int a, b;
int *c = &b, *e = &a;
static int *const *d = &c;
int
main ()
{
    c = e;
    c = *d;
    *c = 1;
    printf ("%d\n", a);
    return 0;
}
