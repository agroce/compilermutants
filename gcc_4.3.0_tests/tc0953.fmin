int printf (const char *, ...);
int a[6];
int b;
static int *c = &a[5];
int
main ()
{
    *c = 1;
    b = a[5];
    *c = 0;
    printf ("%d\n", b);
    return 0;
}
