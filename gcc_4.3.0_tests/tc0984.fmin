int printf (const char *, ...);
int a[4];
static int *b = &a[3];
int
main ()
{
    int *c = &a[3];
    *b = 1;
    *b = *c;
    printf ("%d\n", a[3]);
    return 0;
}
