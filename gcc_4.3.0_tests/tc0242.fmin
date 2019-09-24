int printf (const char *, ...);
int a[4];
static int *b = &a[3], *c = &a[3];
int
main ()
{
    *c = 1;
    *b = a[3];
    printf ("%d\n", a[3]);
    return 0;
}
