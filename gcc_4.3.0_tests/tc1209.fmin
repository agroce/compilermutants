int printf (const char *, ...);
int a[2];
static int *b = &a[1];
int c;
int *d = &c;
int
main ()
{
    int *e = &a[1];
    *b = 1;
    *d = *e;
    *b = 0;
    printf ("%d\n", c);
    return 0;
}
