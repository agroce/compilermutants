int printf (const char *, ...);
int a = 1, e;
int *b = &a;
static int *const *c = &b;
int **d = &b;
int
main ()
{
    int **f = &b;
    *f = &e;
    b = *c;
    **d = 0;
    printf ("%d\n", a);
    return 0;
}
