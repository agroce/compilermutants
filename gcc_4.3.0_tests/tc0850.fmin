int printf (const char *, ...);
char a;
static char *b = &a;
int c;
int
fn1 (p1)
{
    return 1 + p1;
}

int
main ()
{
    c = fn1 (b == &a);
    printf ("%d\n", c);
    return 0;
}
