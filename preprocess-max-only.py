#!/usr/bin/env python3

import csv
import statistics

month_offset = [ 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 ]

daily_maxs = {}

days = {}

with open('data.csv', newline='') as csvfile:
    data = csv.reader(csvfile, delimiter=',')
    for row in data:
        if len(row) < 3:
            continue
        if not '/' in row[0]:
            continue
        (m,d,y) = row[0].split('/')
        daynum = month_offset[int(m)-1] + int(d);
        maxtemp = row[2]
        try:
            maxtemp = int(maxtemp)
            if daynum not in daily_maxs:
                daily_maxs[daynum] = {}
            daily_maxs[daynum][int(y)] = maxtemp
        except ValueError as verr:
          pass

#print("daynum,day,min,max,min50s,max50s,min60s,max60s,min70s,max70")
print("daynum,maxavg,maxs...")
for daynum in range(1, 366):
    if daynum not in daily_maxs:
        continue
    maxday = list(daily_maxs[daynum].items())
    maxs = list(daily_maxs[daynum].items())
    # also take 3 days nearby
    for i in [-3, -2, -1, 1, 2, 3]:
        nearby = daynum + i
        if nearby <= 0:
            nearby += 365
        elif nearby > 365:
            nearby -= 365
        if nearby in daily_maxs:
            maxs += list(daily_maxs[nearby].items())
    avg_max = statistics.mean([ t for (y, t) in maxs ])
    print("%d,%.1f,%s" % (daynum, avg_max, ",".join([("%d" % (t)) for (y, t) in maxday])))
