int printf (const char *, ...);
char a[84];
static char *b = &a[14];
int c;
void
fn1 ()
{
    a[14] = 1;
}

int
main ()
{
    a[14] = 1;
    *b = *b;
    fn1 ();
    *b = *b;
    for (; c < 7; c++)
        printf ("%d\n", a[c * 7]);
    return 0;
}
