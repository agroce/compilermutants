int printf (const char *, ...);
int a;
int *b = &a, *c;
void
fn1 (p1)
{
    int **d = &c;
    *b = (&c == d) < p1;
}

int
main ()
{
    fn1 (4);
    printf ("%d\n", a);
    return 0;
}
