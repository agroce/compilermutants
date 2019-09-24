int printf (const char *, ...);
int a = 1;
int *const b = &a;
static int *const *c = &b;
int
main ()
{
    **c = 0;
    if (*b)
        **c = 0;
    else
        **c = 1;
    printf ("%d\n", a);
    return 0;
}
