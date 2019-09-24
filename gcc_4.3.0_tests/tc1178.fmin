int printf (const char *, ...);
int a = 9, b = 1;
void
fn1 (p1)
{
    if (p1)
        b = 0;
}

int
main ()
{
    int *c = &a;
    fn1 ((&a == c) < *c);
    printf ("%d\n", b);
    return 0;
}
