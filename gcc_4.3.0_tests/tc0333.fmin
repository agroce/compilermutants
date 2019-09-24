int printf (const char *, ...);
int a;
int *b = &a;
char *c;
char d;
void
fn1 (p1)
{
    *b = (c = &d) == &d | p1;
}

int
main ()
{
    fn1 (3);
    printf ("%d\n", a);
    return 0;
}
