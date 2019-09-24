int printf (const char *, ...);
int a;
unsigned ***b;
int *c;
int **d = &c;
void
fn1 (p1)
{
    unsigned ****e = &b;
    c = &a;
    **d = e == &b | p1;
}

int
main ()
{
    fn1 (2);
    printf ("%d\n", a);
    return 0;
}
