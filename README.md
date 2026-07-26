# lightwave

This is a lightwave fork that focuses on adding support for curves, and more specifically, feathers.
In order to get access to some curves and feathers to render with this modified lightwave, the Blender exporter has been extended to export particle system hair as curves.
These exported curves can also be converted into feathers by changing the shape type from "curves" to "feathers" in the .xml file.
There also exists a stand-alone python script that generates feathers (found in the directory _feather\_generation_), but this is mostly a leftover from early tests and pretty out-dated, so in most cases the exporter should be the better choice.

Some features are elaborated on in READMEs in sub-folders:
* [New shapes](./src/shapes/README.md)
* [New materials](./src/shapes/README.md)

## Scripts
There are a few scripts contained in the ``util_scripts`` folder that help out with testing various LOD threshold settings.
* ``modify_scene.py`` renders multiple images with different settings based on what you write into the runs list in the script.
* ``compare_mse.py`` computes the MSE of given images. It outputs three values with varying levels of outlier removal.
* ``compare_mse_multiple.py`` uses above script to compare many images to each other at once. You need to specify which comparisons you want in the runs list.
* ``create_scatterplot_bias_efficiency.py`` creates a scatterplot in your browser based on data from a .csv file that contains rendering results. Your .csv file should have the column titles: "Scene", "LOD Switch", "LOD Switch Value", "Render Time(s)", "MSE (LD vs Reference)", "MSE (64spp LD vs 1024 spp LD)", "Bias" and "Efficiency".

## Overview on feathers
In general, feathers consist of one spine, to which lots of so-called barbs are attached on both sides.
In the same manner, many barbules are attached to barbs on both sides.
The two sides of a feather are probably asymmetric, and all components will curve at least slightly in some way. Between the barbules there is some space, and they lock into each other to make the feather more cohesive.
Groups of barbs can deviate from the general direction a bit to form a group and cause some gaps.

We chose to model all of the components with a single cubic bézier curve each, since this keeps down the complexity, and for most feathers this should be enough.
However, if necessary, extending the code to support multi-segment bézier curves should not be too hard.
Also note that different feather components (spines, barbs, barbules) will always be saved and treated separately, since they tend to differ a bit in their parameters.

## Scenes
The _tests_ directory contains some test scenes, and of these, all tests in the directories _feather_, _multiple\_feathers_, _chicken_ and _owlbear_ are related to curves and feathers.
_feather_ focuses on general curve tests and simple feathers, _multiple\_feathers_ focuses on scenes that contain multiple feathers, _chicken_ and _owlbear_ try to render a whole feathered creature.
Most of the test scenes do not contain the actual test scenes, so you will have to download them yourself from the sources given in the READMEs in each test folder.
The HDRIs used in most scenes can be found here:
- https://polyhaven.com/a/kloofendal_48d_partly_cloudy_puresky
- https://polyhaven.com/a/citrus_orchard_road_puresky

## Testing LOD
A feature that helps visualizing which parts are high and which ones are low detail has been added.
To use it, add a second BSDF in an xml of a shape that uses LOD.
This second BSDF needs the name="lowBsdf" parameter, otherwise the render will crash due to competing BSDF.
It is important that the main BSDF has a very bright color, ideally white, so that the color of the low detail BSDF does not get killed.

The way this works is that if a low level of detail is used, the alternative "low detail" material is evaluated and sampled instead of the normal one.

## Blender exporter
**Warning**: This version of lightwave still uses the old Blender exporter.

The exporter can handle exporting bézier curves and hair particles into curves.
Hair is exported by taking the vertices that mark the beginning and end of individual hair segments, setting them as control points w0 and w3, and then generating handles w1 and w2 that try to approximate the hair segment.
Note that especially for scenes with detailed meshes the export may take a bit longer since the exporter needs to find the corresponding surface triangle for each hair.

Some things to keep in mind when using the exporter:
* The export may take a bit and Blender is not interactive during that time, so it may look like Blender is crashing.
* When using _feathers_, objects with hair that will be converted to feathers should not have any scale transform on them. If you need one, make sure to apply it to the object, so that it does not get exported as a transform, because this will cause the feathers to be scaled down too much.
* When using _feathers_, do not lower the number of segments of hair in Blender to 2. It will crash the comparison functions.

**Important**: 
* When exporting meshes with hair attached to them, you need to ensure the materials are in a specific order, because the exporter will mix up the assignments otherwise. The material that the hair should have will always be taken from the first material slot when listing all the materials of the object. It does not matter which material you assign to the hair inside the particle system menu specifically, it will always use the first material. After that, in any order you can have the remaining materials of the mesh. Make sure that all surfaces have the material you want them to in Blender, otherwise things might easily get mixed up or colors might seem to be missing. Other than that, the ordering of the other materials should not be important.
* If you add new hair to a scene in Blender, for the love of all that you hold dear, make sure the new hairs have the same number of segments/keys as the other hair in that particle system. The render will crash without error if they have different counts.

## New texture
The _ringed_ texture has been added, which is specifically made for curves. It tries to replicate the pattern that feathers of snow owls have, where there is one base color, and many thin rings of a different color which increase in their frequency towards the end of the curve. The properties of this texture are _color0_ (base color) and _color1_ (color of the rings).

# Some warnings
- Generating feathers during the export may result in multiple particle systems getting merged in the result (if there are multiple)

## Contributors
Lightwave was written by [Alexander Rath](https://graphics.cg.uni-saarland.de/people/rath.html), with contributions from [Ömercan Yazici](https://graphics.cg.uni-saarland.de/people/yazici.html) and [Philippe Weier](https://graphics.cg.uni-saarland.de/people/weier.html).
Many of our design decisions were heavily inspired by [Nori](https://wjakob.github.io/nori/), a great educational renderer developed by Wenzel Jakob.
We would also like to thank the teams behind our dependencies: [ctpl](https://github.com/vit-vit/CTPL), [miniz](https://github.com/richgel999/miniz), [stb](https://github.com/nothings/stb), [tinyexr](https://github.com/syoyo/tinyexr), [tinyformat](https://github.com/c42f/tinyformat), [pcg32](https://github.com/wjakob/pcg32), and [catch2](https://github.com/catchorg/Catch2).
