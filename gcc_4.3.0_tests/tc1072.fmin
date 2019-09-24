int printf (const char *, ...);
int a, d, e = 1;
int *b = &a;
static int *const *c = &b;
int
main ()
{
    int **f = &b;
    *f = &e;
    *f = *c;
    d = **f;
    printf ("%d\n", d);
    return 0;
}
