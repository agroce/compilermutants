int printf (const char *, ...);
int a;
short *b;
short **c;
int *d = &a;
int
fn1 (int p1, int p2)
{
    return p1 * p2;
}

int
main ()
{
    *d = fn1 ((c = &b) == &b, 9);
    printf ("%d\n", a);
    return 0;
}
