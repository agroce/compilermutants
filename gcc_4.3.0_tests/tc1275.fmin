int printf (const char *, ...);
int a = 1, d;
int *b = &a;
static int *const *c = &b;
int
main ()
{
    int **e = &b;
    b = &d;
    **c = 0;
    *e = 0;
    printf ("%d\n", a);
    return 0;
}
