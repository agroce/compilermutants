int printf (const char *, ...);
int a;
int *b = &a;
int **const c = &b;
static int **const *d = &c;
int
main ()
{
    **d = 0;
    a = (**d = *c) != 0;
    printf ("%d\n", a);
    return 0;
}
