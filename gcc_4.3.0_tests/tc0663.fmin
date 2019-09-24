int printf (const char *, ...);
int a[2];
int *const b = &a[1];
static int *const *c = &b;
int d;
int
main ()
{
    **c = !*b;
    d = *b;
    **c = 0;
    printf ("%d\n", d);
    return 0;
}
