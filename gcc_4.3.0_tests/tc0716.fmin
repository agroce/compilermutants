int printf (const char *, ...);
int a[2];
int *const b = &a[1];
static int *const *c = &b;
int
main ()
{
    *b = 1;
    **c = 0;
    **c = *b;
    printf ("%d\n", a[1]);
    return 0;
}
