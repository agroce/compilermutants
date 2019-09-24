int printf (const char *, ...);
int a[5];
static int *b = &a[4];
int
main ()
{
    int *c = &a[4];
    *b = 1;
    *b = *c;
    printf ("%d\n", a[4]);
    return 0;
}
