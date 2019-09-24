int printf (const char *, ...);
int a;
static int *b = &a;
static const int *c = &a;
int
main ()
{
    a = 1;
    *b = *c;
    printf ("%d\n", a);
    return 0;
}
