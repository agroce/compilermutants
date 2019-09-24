int printf (const char *, ...);
int a[2];
static int *b = &a[1];
int
main ()
{
    a[1] = 1;
    a[1] = *b;
    printf ("%d\n", a[1]);
    return 0;
}
