int printf (const char *, ...);
int a = 1;
short b;
static const int *c = &a;
short *d = &b;
int
main ()
{
    int *e = &a;
    *e = 0;
    *e = *c;
    *d = *e;
    printf ("%d\n", b);
    return 0;
}
