int printf (const char *, ...);
int a = 1;
static int *b = &a, *d = &a;
static const int *c = &a;
int
main ()
{
    *b = 0;
    *d = *c;
    printf ("%d\n", a);
    return 0;
}
