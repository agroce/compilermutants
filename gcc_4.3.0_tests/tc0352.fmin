int printf (const char *, ...);
int a = 1, e;
int *b = &a;
static int **c = &b;
static int *const *d = &b;
int
main ()
{
    *c = &e;
    **d = 0;
    *c = 0;
    printf ("%d\n", a);
    return 0;
}
