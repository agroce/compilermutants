int printf (const char *, ...);
int a;
int b[9];
static int *c = &b[8], *d = &a;
int
main ()
{
    *c = 1;
    *d = b[8];
    *c = 0;
    printf ("%d\n", a);
    return 0;
}
