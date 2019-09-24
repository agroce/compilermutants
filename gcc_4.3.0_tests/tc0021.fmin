int printf (const char *, ...);
int a[7];
static int *b = &a[6];
int c;
int
main ()
{
    int *d = &a[6];
    *d = 1;
    c = *b;
    *d = c;
    printf ("%d\n", a[6]);
    return 0;
}
