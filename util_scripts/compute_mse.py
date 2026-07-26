#! /usr/bin/env python
# From Alex

METRICS = {
  #'MAPE':  lambda a, f: 100 * abs(a - f) / (a + 1e-3),
  #'SMAPE': lambda a, f: 200 * abs(a - f) / (a + f + 1e-3),
  #'L1':    lambda a, f: abs(a - f),
  #'L2':    lambda a, f: (a - f) ** 2,
  'relMSE':  lambda a, f: (a - f) ** 2 / (a + 1e-2) ** 2
}

import sys
import numpy as np
import Imath
import OpenEXR
import json

def load_image(path):
  file = OpenEXR.InputFile(path)
  FLOAT = Imath.PixelType(Imath.PixelType.FLOAT)
  for prefix in ["", "color."]:
    try:
      return [ np.frombuffer(file.channel(prefix+chan, FLOAT), dtype=np.float32) for chan in ("R", "G", "B") ]
    except:
      continue

im1 = load_image(sys.argv[1])

baseline = None
multi = len(sys.argv) > 3
for argvI in range(2, len(sys.argv)):
  im2path = sys.argv[argvI]
  im2 = load_image(im2path)
  errors = { metric: [] for metric in METRICS.keys() }

  for c in range(3):
    for m, fn in METRICS.items():
      errors[m].append(fn(im1[c], im2[c]))

  for m, fn in METRICS.items():
    errors[m] = np.concatenate(errors[m])

  result = {}
  for m in METRICS:
    errors[m].sort()
    for (f,suffix) in [ (0,""), (0.00001,"*"), (0.0001,"**") ]: # removes no outliers/removes 0.001% outliers/removes 0.01% outliers
      n = errors[m].size
      e = errors[m][0:n-int(n*f)] # ignore fireflies
      name = "{}{}".format(m, suffix)
      result[name] = np.sum(e) / e.size

  print("{}\t{}\t{}".format(result["relMSE"], result["relMSE*"], result["relMSE**"]))