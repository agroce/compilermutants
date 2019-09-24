int printf (const char *, ...);
int a = 1, d;
int *b = &a;
static int *const *c = &b;
int
main ()
{
    int **e = &b, **f = &b;
    *e = &d;
    *f = *c;
    *b = 0;
    printf ("%d\n", a);
    return 0;
}
