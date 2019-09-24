int printf (const char *, ...);
int a, b;
static const int *c = &a;
int *const d = &a, *e = &b;
int
main ()
{
    *d = 1;
    *e = *c;
    *d = 0;
    printf ("%d\n", b);
    return 0;
}
