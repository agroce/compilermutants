int printf (const char *, ...);
int a[3];
static int *b = &a[2];
int
main ()
{
    int *c = &a[2];
    *b = 1;
    *b = *c;
    printf ("%d\n", a[2]);
    return 0;
}
