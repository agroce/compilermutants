int printf (const char *, ...);
int a[7];
int *const b = &a[6];
static int *const *c = &b;
int d;
int
main ()
{
    **c = 1;
    d = *b;
    **c = d;
    printf ("%d\n", a[6]);
    return 0;
}
