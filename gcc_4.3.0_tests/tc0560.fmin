int printf (const char *, ...);
int a;
int *b;
void
fn1 ()
{
    int **c = &b;
    int *d = &a;
    *d = c == &b;
}

int
main ()
{
    int *e = &a;
    fn1 ();
    *e ^= 2;
    printf ("%d\n", a);
    return 0;
}
