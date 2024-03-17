This MicroSD card is part of an art installation.
For details, please contact:
  Victor Pacheco (vpacheco@holycross.edu)
  or Kevin Walsh (kwalsh@holycross.edu).

The arduino will expect a file named "daily.csv" on the MicroSD card, in plain
text formatted as a comma-separated values file. It should have three or more
columns: date (mm/dd/yy or mm/dd/yyyy), minimum daily temperature (degrees F, as
an integer), and maximum daily temperature (degrees F, as an integer).

The first line is the column headers, e.g. "Date, Min, Max". This is ignored.
Each other line looks like "3/15/50,14,42" to indicate that on March 15, 1950,
the minimum temp was 14F, maximum was 42F

For each day, we consider Weekly Minimum Temperature and Weekly Maximum
Temperature for each year and averaged over all years. For example, for 3/15, we
consider the seven day interval 3/12, 3/13, 3/14, 3/15, 3/16, 3/17, and 3/18,
and find the minimum temperature in that interval for each year:
  1950 minimum for week around 3/15 is 20.00 F
  1951 minimum for week around 3/15 is 33.43 F
  1952 minimum for week around 3/15 is 41.71 F
  ...
  1970 minimum for week around 3/15 is 24.86 F
  Average minimum 1950-1970 for week around 3/15 is 17.43 F
