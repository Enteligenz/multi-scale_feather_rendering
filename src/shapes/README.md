# Additions to shapes
The new shapes are Feathers and Curves, found in feathers.cpp and curves.hpp, respectively.

## Curves
As the plural in the name implies, this is actually a collection of curve primitives, usually ones attached to the same surface.
It is assumed that all curves in this collection have the same parameters in terms of length, number of segments and thickness.

There are some optional parameters that can be set in the .xml file:
* _curvesplits_: The raytracing algorithm for feathers splits each curve into multiple segments, the number of which is determined by this parameter. The default is 16. It should be noted that more splits mean increased parsing time and shorter render time, and vice versa.
* _rootradius_: The radius that curves have at their base (where they touch the surface, usually). The default is 0.0005.
* _tipradius_: The radius that curves have at their tip. The default is 0.0001.
* _radiusfunction_: A string that chooses which of the implemented functions to use (linear/power/sigmoid). The function modifies how the radius between root and tip of a curve is calculated. The default is "sigmoid".

## Feathers
Similar to Curves, Feathers are a collection of feathers.
These feathers consist of a group of Curves, one Curves object for each component type (shaft, barbs, barbules).

There are some optional parameters that can be set in the .xml file:
* ``lod``: Whether level of detail should be used (true) or not (false). The default is true.
* ``thresholdLOD``: The threshold that determines when a lower level of detail is used. What value this should be is very dependent on which LOD switch is being used. Currently the switch can only be changed by commenting in the fitting line in accel.hpp and re-building (see intersect() at line 426 in accel.hpp).
* ``numOriginalFeathers``: How many feathers should be properly generated. The default is 2.
* ``rootRadiusSpine``: What radius the root of the feather spine/shaft should have.
* ``asymmetry``: How much asymmetry the feather should have. Values should roughly stay around [-1, 1], as more extreme values can make the feather look strange. Negative values make the left side shorter, positives the right side.
* ``planeFindingMode``: Which algorithm for finding a plane from a data cloud should be used. This is relevant for finding low detail feathers. The options are standard, pca and svd. The default is pca. My written thesis has only used PCA, so I recommend that option.
* ``barbAngle``: How strongly the barbs angle forward towards the tip, 0 being the lowest and 1 being the strongest. The default is 0.4.
* ``barbLengthRatio``: How long the barbs should be in relation to the spine. The default is 0.4.
* ``directionDerivativePos``: When instancing feathers a direction vector is needed to rotate the original feather onto the target curve. This direction vector is found by taking the derivative at _directionDerivativePos_ along the curve. The value should be between 0 and 1, the closer to 1 it is, the further it is towards the last control point of the curve. The default is 0.
* ``isTailFeather``: When true, a tail feather will be generated. Otherwise, a wing feather.

The way _feathers_ generates feathers from exported curves is as follows:
It treats each hair as a potential feather spine.
It then generates a small number of feathers around some of those spines, trying to come up with a selection that is as diverse as possible.
Then, it converts the remaining hair by replacing them with instances of previously generated hairs.
It tries to match feathers that are similar, but still involves some randomness.
A slight scale transform is applied to these instances to create some variety.

``m_a``, ``m_b`` and ``m_c`` are some optional parameters that are used by one of the LOD switching metrics.

# LOD Thresholds
_accel.cpp_ contains a modified intersection for acceleration structures that switches the level of detail based on various metrics.
To change which metric is used, the line containing that metric needs to be commented in and all others need to be commented out.
Lines containing the metrics are recognizable by the number in square brackets [x] at their end.
Currently these switching metrics are implemented:
1. **distPDF**: Uses a combination of the last PDF (see 2.) and the traveled distance (see 3.).
2. **PDF**: Uses the PDF from the last surface intersection.
3. **Distance**: (both [3] and [3.5]) Uses the sum of the distance the current ray has traveled so far plus the estimated distance of the current bounce.
4. **Depth**: Uses the current bounce depth of the ray.
5. **Always low detail/OnlyLD**: Always uses low detail, no matter what.
6. **Footprint**: Projects a camera pixel into the scene along the path the ray has traveled and uses the resulting footprint area.
7. **distPDFDepth**: Metric as found in Philipp Ziegler's Bsc Thesis which uses a weighted combination of PDF, traveled distance and bounce depth.
8. **distPDFWeighted**: Same as 1., but distance value is weighted down.