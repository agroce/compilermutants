int printf (const char *, ...);
int a, f;
int *b, *e = &a;
int **const c = &b, **g = &b;
static int **const *d = &c;
int
main ()
{
    *g = &f;
    **d = e;
    **d = *c;
    **c = 1;
    printf ("%d\n", a);
    return 0;
}
