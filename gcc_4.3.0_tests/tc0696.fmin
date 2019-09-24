int printf (const char *, ...);
int a[5];
static int *b = &a[4];
int c;
int
main ()
{
    int *d = &a[4];
    *b = 1;
    c = *d;
    *b = 0;
    printf ("%d\n", c);
    return 0;
}
