int printf (const char *, ...);
int a;
static int b = 7;
int *const c = &a;
int
main ()
{
    int *d = &a;
    *c = d != c ^ b;
    printf ("%d\n", a);
    return 0;
}
