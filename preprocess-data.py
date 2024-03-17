#!/usr/bin/env python3

import csv
import statistics

month_offset = [ 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 ]

daily_mins = {}
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
        days[daynum] = m + "/" + d
        mintemp = row[1]
        maxtemp = row[2]
        #print("%s/%s/%s day %d min %s hi %s" % (m, d, y, daynum, mintemp, maxtemp))
        try:
            mintemp = int(mintemp)
            if daynum not in daily_mins:
                daily_mins[daynum] = {}
            daily_mins[daynum][int(y)] = mintemp
        except ValueError as verr:
          pass
        try:
            maxtemp = int(maxtemp)
            if daynum not in daily_maxs:
                daily_maxs[daynum] = {}
            daily_maxs[daynum][int(y)] = maxtemp
        except ValueError as verr:
          pass

#print("daynum,day,min,max,min50s,max50s,min60s,max60s,min70s,max70")
print("daynum,min,max,min50s,max50s,min60s,max60s,min70s,max70")
for daynum in range(1, 366):
    if daynum not in daily_mins:
        # print("day %d ... no info" % (daynum))
        continue
    mins = list(daily_mins[daynum].items())
    maxs = list(daily_maxs[daynum].items())
    # also take 3 days nearby
    for i in [-3, -2, -1, 1, 2, 3]:
        nearby = daynum + i
        if nearby <= 0:
            nearby += 365
        elif nearby > 365:
            nearby -= 365
        if nearby in daily_mins:
            mins += list(daily_mins[nearby].items())
        if nearby in daily_maxs:
            maxs += list(daily_maxs[nearby].items())
    avg_min = statistics.mean([ t for (y, t) in mins ])
    avg_max = statistics.mean([ t for (y, t) in maxs ])
    avg_min50 = statistics.mean([ t for (y, t) in mins if 50 <= y and y < 60])
    avg_max50 = statistics.mean([ t for (y, t) in maxs if 50 <= y and y < 60])
    avg_min60 = statistics.mean([ t for (y, t) in mins if 60 <= y and y < 70])
    avg_max60 = statistics.mean([ t for (y, t) in maxs if 60 <= y and y < 70])
    avg_min70 = statistics.mean([ t for (y, t) in mins if y == 70])
    avg_max70 = statistics.mean([ t for (y, t) in maxs if y == 70])
    avg_by_yr = []
    for yr in range(50, 71):
        a = statistics.mean([ t for (y, t) in mins if y == yr])
        b = statistics.mean([ t for (y, t) in maxs if y == yr])
        avg_by_yr += [a, b]
    row = [ avg_min, avg_max, avg_min50, avg_max50, avg_min60, avg_max60, avg_min70, avg_max70 ]
    #row.append(avg_by_yr)
    #print("%d,%s,%s" % (daynum, days[daynum], ",".join(["%.1f" % (x) for x in row])))
    print("%d,%s" % (daynum, ",".join(["%.1f" % (x) for x in row])))
