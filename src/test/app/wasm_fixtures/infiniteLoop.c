int loop()
{
  int volatile x = 0;
  while (1)
    x++;
  return x;
}
