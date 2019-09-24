int printf (const char *, ...);
int a, d, f, i;
int *b = &a, *g = &d, *h;
static int **c = &b;
static int *const *e = &b;
int
main ()
{
    h = *c;
    *c = &f;
    **e = 1;
    i = *h;
    *c = &i;
    *g = *b;
    printf ("%d\n", d);
    return 0;
}
