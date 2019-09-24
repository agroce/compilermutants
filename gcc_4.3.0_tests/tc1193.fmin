int printf (const char *, ...);
int a;
int *const b = &a;
static int *const *c = &b;
int
main ()
{
    *b = 1;
    *b = **c;
    printf ("%d\n", a);
    return 0;
}
